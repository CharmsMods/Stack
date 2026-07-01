#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace {

inline void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SafeLog2(float value) {
    return std::log2(std::max(value, 0.000001f));
}

float PercentileFromSorted(const std::vector<float>& sorted, float percentile) {
    if (sorted.empty()) {
        return 0.0f;
    }
    const float p = std::clamp(percentile, 0.0f, 1.0f);
    const float scaled = p * static_cast<float>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t hi = std::min<std::size_t>(lo + 1, sorted.size() - 1);
    const float t = scaled - static_cast<float>(lo);
    return sorted[lo] * (1.0f - t) + sorted[hi] * t;
}

float LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

RenderPipeline::AutoGainSceneStats RenderPipeline::ComputeAutoGainSceneStats(unsigned int inputTexture) {
    AutoGainSceneStats fallback;
    if (!inputTexture || m_Width <= 0 || m_Height <= 0) {
        return fallback;
    }

    std::size_t cacheKey = HashValue(inputTexture);
    HashCombine(cacheKey, HashValue(m_Width));
    HashCombine(cacheKey, HashValue(m_Height));
    if (const auto cached = m_AutoGainSceneStatsCache.find(cacheKey);
        cached != m_AutoGainSceneStatsCache.end()) {
        return cached->second;
    }

    EnsureAutoGainStatsProgram();
    if (!m_AutoGainStatsProgram) {
        return fallback;
    }

    const int statsWidth = std::clamp(m_Width / 32, 64, 160);
    const int statsHeight = std::clamp(m_Height / 32, 36, 120);
    const unsigned int statsTexture = GLHelpers::CreateEmptyTexture(statsWidth, statsHeight);
    if (!statsTexture) {
        return fallback;
    }
    const unsigned int statsFbo = GLHelpers::CreateFBO(statsTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, statsFbo);
    glViewport(0, 0, statsWidth, statsHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_AutoGainStatsProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_AutoGainStatsProgram, "uInputImage"), 0);
    glUniform2f(glGetUniformLocation(m_AutoGainStatsProgram, "uSourceTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
    m_Quad.Draw();

    std::vector<float> pixels(static_cast<std::size_t>(statsWidth) * static_cast<std::size_t>(statsHeight) * 4u, 0.0f);
    glReadPixels(0, 0, statsWidth, statsHeight, GL_RGBA, GL_FLOAT, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glDeleteFramebuffers(1, &statsFbo);
    glDeleteTextures(1, &statsTexture);

    std::vector<float> lumas;
    lumas.reserve(pixels.size() / 4);
    float clipped = 0.0f;
    float saturated = 0.0f;
    float textureSum = 0.0f;
    float darkTextureSum = 0.0f;
    float darkCount = 0.0f;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const float lum = pixels[i + 0];
        const float maxChannel = pixels[i + 1];
        const float saturation = pixels[i + 2];
        const float texture = pixels[i + 3];
        if (!std::isfinite(lum) || lum <= 0.0f) {
            continue;
        }
        lumas.push_back(lum);
        clipped += maxChannel > 1.0f ? 1.0f : 0.0f;
        saturated += (maxChannel > 0.35f && saturation > 0.58f) ? 1.0f : 0.0f;
        textureSum += std::clamp(texture, 0.0f, 1.0f);
        if (lum < 0.08f) {
            darkTextureSum += std::clamp(texture, 0.0f, 1.0f);
            darkCount += 1.0f;
        }
    }

    AutoGainSceneStats stats = fallback;
    if (lumas.size() < 16) {
        m_AutoGainSceneStatsCache[cacheKey] = stats;
        return stats;
    }
    std::sort(lumas.begin(), lumas.end());
    const float count = static_cast<float>(lumas.size());
    stats.shadowPercentile = std::max(PercentileFromSorted(lumas, 0.10f), 0.00001f);
    stats.midtonePercentile = std::max(PercentileFromSorted(lumas, 0.50f), 0.00001f);
    stats.highlightPercentile = std::max(PercentileFromSorted(lumas, 0.98f), 0.00001f);
    const float p01 = std::max(PercentileFromSorted(lumas, 0.01f), 0.000001f);
    const float p05 = std::max(PercentileFromSorted(lumas, 0.05f), 0.000001f);
    stats.clippingRatio = Clamp01(clipped / count);
    stats.channelSaturationRatio = Clamp01(saturated / count);
    stats.textureConfidence = Clamp01(textureSum / count);

    const float darkTexture = darkCount > 0.0f ? darkTextureSum / darkCount : stats.textureConfidence;
    stats.estimatedNoiseFloor = std::clamp(std::max(p01, p05 * 0.52f) * (1.0f + darkTexture * 2.85f), 0.00003f, 0.10f);
    const float noiseRisk = Clamp01((stats.estimatedNoiseFloor * 7.0f) / std::max(stats.shadowPercentile, 0.00003f));
    const float highlightPressure = Clamp01(std::max(stats.clippingRatio * 8.0f, stats.channelSaturationRatio * 2.5f) +
        std::max(0.0f, SafeLog2(stats.highlightPercentile / 0.85f)) * 0.30f);

    const float target = 0.30f;
    const float adaptiveTarget = std::clamp(
        target + LerpFloat(0.01f, -0.07f, highlightPressure) - noiseRisk * 0.04f,
        0.22f,
        0.42f);
    const float highlightTarget = LerpFloat(0.80f, 0.62f, highlightPressure);
    stats.recommendedMinEv = std::clamp(SafeLog2(highlightTarget / stats.highlightPercentile) - highlightPressure * 0.35f, -2.25f, 0.5f);
    const float shadowTarget = LerpFloat(0.18f, 0.30f, 1.0f - noiseRisk);
    float maxLift = SafeLog2(shadowTarget / std::max(stats.shadowPercentile, stats.estimatedNoiseFloor * 8.0f));
    maxLift *= LerpFloat(0.85f, 0.45f, noiseRisk);
    const float conservativeLiftCap = LerpFloat(1.75f, 1.05f, noiseRisk);
    stats.recommendedMaxEv = std::clamp(maxLift, 0.25f, conservativeLiftCap);
    if (stats.recommendedMaxEv < stats.recommendedMinEv + 0.25f) {
        stats.recommendedMaxEv = std::min(2.0f, stats.recommendedMinEv + 0.25f);
    }
    // Keep the auto baseline scene-referred and restrained. Pre-Local Exposure should provide
    // a usable local-exposure starting point, not behave like aggressive global exposure
    // compensation or promise true clipped-highlight recovery.
    float recommendedBaseEv = SafeLog2(adaptiveTarget / stats.midtonePercentile);
    recommendedBaseEv *= LerpFloat(0.95f, 0.70f, std::max(highlightPressure, noiseRisk));
    stats.recommendedBaseEv = std::clamp(recommendedBaseEv, -1.0f, 1.0f);
    stats.recommendedNoiseProtection = Clamp01(0.55f + noiseRisk * 0.45f + darkTexture * 0.20f);
    stats.recommendedHighlightProtection = Clamp01(0.78f + highlightPressure * 0.22f + stats.channelSaturationRatio * 0.18f);
    stats.recommendedShadowLiftLimit = Clamp01(0.68f - noiseRisk * 0.34f);
    stats.recommendedTarget = adaptiveTarget;
    stats.valid = true;

    m_AutoGainSceneStatsCache[cacheKey] = stats;
    return stats;
}

Raw::RawDetailFusionSettings RenderPipeline::ResolveAutoGainEffectiveSettings(
    unsigned int inputTexture,
    const Raw::RawDetailFusionSettings& settings) {
    const auto sanitizeNaturalSettings = [](Raw::RawDetailFusionSettings value) {
        const bool explicitEvWindow = value.overrideMinEv || value.overrideMaxEv;
        if (explicitEvWindow) {
            value.minEv = std::clamp(value.minEv, -4.0f, 0.5f);
            value.maxEv = std::clamp(value.maxEv, 0.0f, 4.0f);
            if (value.maxEv < value.minEv) {
                value.maxEv = value.minEv;
            }
        } else {
            value.minEv = std::clamp(value.minEv, -2.5f, 0.5f);
            value.maxEv = std::clamp(value.maxEv, std::max(value.minEv + 0.01f, 0.25f), 2.5f);
        }
        value.baseEv = std::clamp(value.baseEv, -1.0f, 1.0f);
        value.minEvBias = std::clamp(value.minEvBias, -2.0f, 2.0f);
        value.maxEvBias = std::clamp(value.maxEvBias, -2.0f, 2.0f);
        value.baseEvBias = std::clamp(value.baseEvBias, -1.25f, 1.25f);
        value.wellExposedTargetBias = std::clamp(value.wellExposedTargetBias, -1.0f, 1.0f);
        value.strength = std::clamp(value.strength, 0.0f, 1.25f);
        value.baseRadiusPercent = std::clamp(value.baseRadiusPercent, 0.002f, 0.030f);
        value.wellExposedTarget = std::clamp(value.wellExposedTarget, 0.10f, 0.55f);
        return value;
    };

    Raw::RawDetailFusionSettings effective = settings;
    if (!settings.autoSafetyEnabled) {
        return sanitizeNaturalSettings(effective);
    }

    const AutoGainSceneStats stats = ComputeAutoGainSceneStats(inputTexture);
    if (!stats.valid) {
        return sanitizeNaturalSettings(effective);
    }

    effective.minEv = settings.overrideMinEv
        ? settings.minEv
        : std::clamp(stats.recommendedMinEv + settings.minEvBias, -2.25f, 0.5f);
    effective.maxEv = settings.overrideMaxEv
        ? settings.maxEv
        : std::clamp(stats.recommendedMaxEv + settings.maxEvBias, 0.25f, 2.5f);
    const bool explicitEvWindow = settings.overrideMinEv || settings.overrideMaxEv;
    if (explicitEvWindow && effective.maxEv < effective.minEv) {
        effective.maxEv = effective.minEv;
    } else if (!explicitEvWindow && effective.maxEv < effective.minEv + 0.25f) {
        effective.maxEv = std::min(2.5f, effective.minEv + 0.25f);
    }
    effective.baseEv = settings.overrideBaseEv
        ? settings.baseEv
        : std::clamp(stats.recommendedBaseEv + settings.baseEvBias, -1.0f, 1.0f);
    effective.noiseProtection = settings.overrideNoiseProtection
        ? settings.noiseProtection
        : Clamp01(stats.recommendedNoiseProtection + settings.noiseProtectionBias);
    effective.highlightProtection = settings.overrideHighlightProtection
        ? settings.highlightProtection
        : Clamp01(stats.recommendedHighlightProtection + settings.highlightProtectionBias);
    effective.shadowLiftLimit = settings.overrideShadowLiftLimit
        ? settings.shadowLiftLimit
        : Clamp01(stats.recommendedShadowLiftLimit - settings.shadowLiftLimitBias);
    effective.wellExposedTarget = settings.overrideWellExposedTarget
        ? settings.wellExposedTarget
        : std::clamp(stats.recommendedTarget + settings.wellExposedTargetBias, 0.10f, 0.55f);
    return sanitizeNaturalSettings(effective);
}

RenderPipeline::PreLocalExposureSummary RenderPipeline::BuildPreLocalExposureSummary(
    unsigned int inputTexture,
    const Raw::RawDetailFusionSettings& settings,
    bool legacyMaskActive,
    bool legacyManualMode) {
    PreLocalExposureSummary summary;
    if (!inputTexture) {
        return summary;
    }

    const AutoGainSceneStats stats = ComputeAutoGainSceneStats(inputTexture);
    if (!stats.valid) {
        return summary;
    }

    const Raw::RawDetailFusionSettings effective = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    const float noiseRisk = Clamp01((stats.estimatedNoiseFloor * 7.0f) / std::max(stats.shadowPercentile, 0.00003f));
    const float highlightPressure = Clamp01(
        std::max(stats.clippingRatio * 8.0f, stats.channelSaturationRatio * 2.5f) +
        std::max(0.0f, SafeLog2(stats.highlightPercentile / 0.85f)) * 0.30f);
    const float gradientPressure = Clamp01((1.0f - stats.textureConfidence) * 0.85f + effective.smoothGradientProtection * 0.35f);

    summary.valid = true;
    summary.effectiveSettings = effective;
    summary.clippingRatio = stats.clippingRatio;
    summary.channelSaturationRatio = stats.channelSaturationRatio;
    summary.estimatedNoiseFloor = stats.estimatedNoiseFloor;
    summary.shadowPercentile = stats.shadowPercentile;
    summary.highlightPercentile = stats.highlightPercentile;
    summary.textureConfidence = stats.textureConfidence;
    summary.noiseLimited = noiseRisk > 0.35f;
    summary.highlightLimited = highlightPressure > 0.25f;
    summary.gradientProtected = effective.smoothGradientProtection > 0.60f && gradientPressure > 0.55f;
    summary.legacyMaskActive = legacyMaskActive;
    summary.legacyManualMode = legacyManualMode;
    return summary;
}

unsigned int RenderPipeline::RenderRawDetailAutoMask(
    unsigned int inputTexture,
    const RenderGraphNode& node,
    unsigned int manualMaskTexture,
    bool debugPreview) {
    EnsureRawDetailFusionPrograms();
    if (!inputTexture || !m_RawDetailFusionAnalysisProgram || !m_RawDetailFusionMetricsProgram ||
        !m_RawDetailFusionSmoothProgram || (debugPreview && !m_RawDetailFusionApplyProgram)) {
        return 0;
    }
    const Raw::RawDetailFusionSettings& settings = node.kind == RenderGraphNodeKind::RawDetailFusion
        ? node.rawDetailFusion.settings
        : node.rawDetailAutoMask.settings;
    const AutoGainSceneStats sceneStats = ComputeAutoGainSceneStats(inputTexture);
    const Raw::RawDetailFusionSettings effectiveSettings = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    const unsigned int metricsTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    const unsigned int analysisTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    const unsigned int smoothTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (!metricsTexture || !analysisTexture || !smoothTexture) {
        if (metricsTexture) glDeleteTextures(1, &metricsTexture);
        if (analysisTexture) glDeleteTextures(1, &analysisTexture);
        if (smoothTexture) glDeleteTextures(1, &smoothTexture);
        return 0;
    }

    auto renderPass = [&](unsigned int texture, const std::function<void(unsigned int)>& fn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        fn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    };

    renderPass(metricsTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionMetricsProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uInputImage"), 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uTextureSensitivity"), settings.textureSensitivity);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uSkyBias"), settings.skyBias);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uAutoNoiseProtection"), effectiveSettings.noiseProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uAutoHighlightProtection"), effectiveSettings.highlightProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionMetricsProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    renderPass(analysisTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionAnalysisProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, metricsTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMetrics"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, manualMaskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uManualMask"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uHasManualMask"), manualMaskTexture ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMode"),
            static_cast<int>(manualMaskTexture ? Raw::RawDetailFusionMode::Hybrid : Raw::RawDetailFusionMode::AutoAnalyze));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMinEv"), effectiveSettings.minEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaxEv"), effectiveSettings.maxEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uBaseEv"), effectiveSettings.baseEv);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSampleCount"), std::clamp(effectiveSettings.sampleCount, 2, 33));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uBaseRadiusPercent"), effectiveSettings.baseRadiusPercent);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uHighlightProtection"), effectiveSettings.highlightProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uShadowLiftLimit"), effectiveSettings.shadowLiftLimit);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uNoiseProtection"), effectiveSettings.noiseProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uDetailWeight"), settings.detailWeight);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uWellExposedTarget"), effectiveSettings.wellExposedTarget);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uSkyBias"), settings.skyBias);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uClippingRatio"), sceneStats.clippingRatio);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uInvertMask"), settings.invertMask ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskBlackPoint"), settings.maskBlackPoint);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskWhitePoint"), settings.maskWhitePoint);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uMaskGamma"), settings.maskGamma);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uManualBlend"), settings.manualBlend);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionAnalysisProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    renderPass(smoothTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionSmoothProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, analysisTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uAnalysis"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, metricsTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uMetrics"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uInputImage"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uRadius"), std::clamp(settings.smoothnessRadius, 0, 16));
        glUniform1i(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uSmoothAreaRadius"), std::clamp(settings.smoothAreaRadius, 0, 32));
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uEdgeAwareness"), settings.edgeAwareness);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uHaloGuard"), settings.haloGuard);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uSmoothGradientProtection"), settings.smoothGradientProtection);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uMaskDebandDither"), settings.maskDebandDither);
        glUniform2f(glGetUniformLocation(m_RawDetailFusionSmoothProgram, "uTexelSize"), 1.0f / std::max(1, m_Width), 1.0f / std::max(1, m_Height));
        m_Quad.Draw();
    });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    if (debugPreview) {
        const unsigned int previewTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
        if (previewTexture) {
            renderPass(previewTexture, [&](unsigned int) {
                glUseProgram(m_RawDetailFusionApplyProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, inputTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uInputImage"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, smoothTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uExposureMap"), 1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, metricsTexture);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMetrics"), 2);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uHasMask"), 1);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMinEv"), effectiveSettings.minEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaxEv"), effectiveSettings.maxEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uBaseEv"), effectiveSettings.baseEv);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uStrength"), effectiveSettings.strength);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
                glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uDebugView"), static_cast<int>(settings.debugView));
                glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaskOutput"), 1);
                m_Quad.Draw();
            });
            glDeleteTextures(1, &smoothTexture);
            glDeleteTextures(1, &analysisTexture);
            glDeleteTextures(1, &metricsTexture);
            glUseProgram(0);
            glActiveTexture(GL_TEXTURE0);
            return previewTexture;
        }
    }
    glDeleteTextures(1, &analysisTexture);
    glDeleteTextures(1, &metricsTexture);
    return smoothTexture;
}

unsigned int RenderPipeline::RenderRawDetailFusion(
    unsigned int inputTexture,
    unsigned int maskTexture,
    const Raw::RawDetailFusionSettings& settings) {
    EnsureRawDetailFusionPrograms();
    if (!inputTexture || !m_RawDetailFusionApplyProgram) {
        return 0;
    }
    const unsigned int outputTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    if (!outputTexture) {
        return 0;
    }
    const AutoGainSceneStats sceneStats = ComputeAutoGainSceneStats(inputTexture);
    const Raw::RawDetailFusionSettings effectiveSettings = ResolveAutoGainEffectiveSettings(inputTexture, settings);
    auto renderPass = [&](unsigned int texture, const std::function<void(unsigned int)>& fn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, m_Width, m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        fn(fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    };

    renderPass(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDetailFusionApplyProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uInputImage"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uExposureMap"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMetrics"), 2);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uHasMask"), maskTexture ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMinEv"), effectiveSettings.minEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaxEv"), effectiveSettings.maxEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uBaseEv"), effectiveSettings.baseEv);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uStrength"), effectiveSettings.strength);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uEstimatedNoiseFloor"), sceneStats.estimatedNoiseFloor);
        glUniform1f(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uChannelSaturationRisk"), sceneStats.channelSaturationRatio);
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uDebugView"), static_cast<int>(Raw::RawDetailFusionDebugView::FinalImage));
        glUniform1i(glGetUniformLocation(m_RawDetailFusionApplyProgram, "uMaskOutput"), 0);
        m_Quad.Draw();
    });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    return outputTexture;
}
