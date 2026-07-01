#include "ToneLayers.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SafeLog2(float value) {
    return std::log2(std::max(value, 0.000001f));
}

float LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
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

std::size_t HashToneCurveJson(const json& value) {
    return std::hash<std::string>{}(value.dump());
}

struct ScopedFramebufferState {
    GLint framebuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };

    ScopedFramebufferState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
    }

    void Restore() const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glReadBuffer(static_cast<GLenum>(readBuffer));
        glDrawBuffer(static_cast<GLenum>(drawBuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

} // namespace

void ToneCurveLayer::SetAutoRewriteRenderContext(int nodeId, std::uint64_t requestRevision) {
    m_AutoRewriteNodeId = nodeId;
    m_AutoRewriteRequestRevision = requestRevision;
}

void ToneCurveLayer::SetDevelopScenePrepToneBudget(bool scenePrepApplied, float strength, float maxEvBias) {
    m_DevelopScenePrepToneBudgetActive = scenePrepApplied;
    m_DevelopScenePrepToneBudgetStrength = std::clamp(strength, 0.0f, 1.25f);
    m_DevelopScenePrepToneBudgetMaxEvBias = std::clamp(maxEvBias, -2.0f, 2.0f);
}

ToneCurveAutoRewriteFeedback ToneCurveLayer::TakePendingAutoRewriteFeedback() {
    ToneCurveAutoRewriteFeedback feedback = m_PendingAutoRewriteFeedback;
    ClearPendingAutoRewriteFeedback();
    return feedback;
}

void ToneCurveLayer::ApplyAutoRewriteFeedback(const ToneCurveAutoRewriteFeedback& feedback) {
    if (!feedback.valid) {
        return;
    }

    if (feedback.authoredLayerJson.is_object()) {
        Deserialize(feedback.authoredLayerJson);
    }

    m_AutoSceneStatsValid = feedback.statsValid;
    m_AutoSceneStats.valid = feedback.statsValid;
    m_AutoSceneStats.shadowPercentile = feedback.shadowPercentile;
    m_AutoSceneStats.midtonePercentile = feedback.midtonePercentile;
    m_AutoSceneStats.highlightPercentile = feedback.highlightPercentile;
    m_AutoSceneStats.clippingRatio = feedback.clippingRatio;
    m_AutoSceneStats.noiseRisk = feedback.noiseRisk;
    m_AutoSceneStats.highlightPressure = feedback.highlightPressure;
    m_AutoSceneStats.textureConfidence = feedback.textureConfidence;
    m_AutoSceneStats.hdrSpreadEv = feedback.hdrSpreadEv;
    m_AutoSceneStats.profile = static_cast<ToneCurveAutoSceneProfile>(std::clamp(
        feedback.sceneProfile,
        static_cast<int>(ToneCurveAutoSceneProfile::Balanced),
        static_cast<int>(ToneCurveAutoSceneProfile::NoisyLowLight)));

    m_AutoSceneShadowPercentile = feedback.shadowPercentile;
    m_AutoSceneMidtonePercentile = feedback.midtonePercentile;
    m_AutoSceneHighlightPercentile = feedback.highlightPercentile;
    m_AutoSceneClippingRatio = feedback.clippingRatio;
    m_AutoSceneNoiseRisk = feedback.noiseRisk;
    m_AutoSceneHighlightPressure = feedback.highlightPressure;
    m_AutoSceneTextureConfidence = feedback.textureConfidence;
    m_AutoSceneHdrSpreadEv = feedback.hdrSpreadEv;
    m_AutoSceneProfile = m_AutoSceneStats.profile;
    m_AutoRecommendedBaseEv = feedback.recommendedBaseEv;
    m_AutoRecommendedLocalStrength = feedback.recommendedLocalStrength;
    m_AutoRecommendedShadowOpening = feedback.recommendedShadowOpening;
    m_AutoRecommendedHighlightCompression = feedback.recommendedHighlightCompression;
    for (int i = 0; i < 5; ++i) {
        m_AutoRecommendedFoundationEv[static_cast<std::size_t>(i)] = feedback.recommendedFoundationEv[i];
    }
    m_AutoSceneStats.recommendedBaseEv = feedback.recommendedBaseEv;
    m_AutoSceneStats.recommendedLocalStrength = feedback.recommendedLocalStrength;
    m_AutoSceneStats.recommendedShadowOpening = feedback.recommendedShadowOpening;
    m_AutoSceneStats.recommendedHighlightCompression = feedback.recommendedHighlightCompression;
    for (int i = 0; i < 5; ++i) {
        m_AutoSceneStats.recommendedFoundationEv[static_cast<std::size_t>(i)] = feedback.recommendedFoundationEv[i];
    }
}

void ToneCurveLayer::RequestAutoCalibration(ToneCurveAutoVariant variant, bool forceReanalysis) {
    m_AutoCalibrateVariant = variant;
    m_AutoCalibratePending = true;
    m_AutoCalibrateForceReanalysis = forceReanalysis;
    ++m_AutoCalibrateRequestId;
    if (forceReanalysis) {
        m_AutoSceneAnalysisFramesUntilRefresh = 0;
    }
}

void ToneCurveLayer::UpdateAutoSceneAnalysis(unsigned int inputTexture, int width, int height, bool forceRefresh) {
    if (!m_AutoCalibratePending || !inputTexture || width <= 0 || height <= 0) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        m_AutoSceneAnalysisTexture = 0;
        m_AutoSceneAnalysisWidth = 0;
        m_AutoSceneAnalysisHeight = 0;
        m_AutoSceneAnalysisFramesUntilRefresh = 0;
        return;
    }

    const ScopedFramebufferState savedState;

    const bool sameInput =
        m_AutoSceneStatsValid &&
        m_AutoSceneAnalysisTexture == inputTexture &&
        m_AutoSceneAnalysisWidth == width &&
        m_AutoSceneAnalysisHeight == height;
    if (sameInput && m_AutoSceneAnalysisFramesUntilRefresh > 0 && !forceRefresh) {
        --m_AutoSceneAnalysisFramesUntilRefresh;
        return;
    }

    const int statsWidth = std::clamp(width / 32, 64, 160);
    const int statsHeight = std::clamp(height / 32, 36, 120);
    const unsigned int statsTexture = GLHelpers::CreateEmptyTexture(statsWidth, statsHeight);
    const unsigned int sourceFbo = GLHelpers::CreateFBO(inputTexture);
    const unsigned int statsFbo = GLHelpers::CreateFBO(statsTexture);
    if (!statsTexture || !sourceFbo || !statsFbo) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        savedState.Restore();
        if (sourceFbo) glDeleteFramebuffers(1, &sourceFbo);
        if (statsFbo) glDeleteFramebuffers(1, &statsFbo);
        if (statsTexture) glDeleteTextures(1, &statsTexture);
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, statsFbo);
    const GLenum sourceStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    const GLenum statsStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (sourceStatus != GL_FRAMEBUFFER_COMPLETE || statsStatus != GL_FRAMEBUFFER_COMPLETE) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        savedState.Restore();
        glDeleteFramebuffers(1, &sourceFbo);
        glDeleteFramebuffers(1, &statsFbo);
        glDeleteTextures(1, &statsTexture);
        return;
    }

    glBlitFramebuffer(0, 0, width, height, 0, 0, statsWidth, statsHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    std::vector<float> pixels(static_cast<std::size_t>(statsWidth) * static_cast<std::size_t>(statsHeight) * 4u, 0.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, statsFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, statsWidth, statsHeight);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, statsWidth, statsHeight, GL_RGBA, GL_FLOAT, pixels.data());
    const bool readbackOk = glGetError() == GL_NO_ERROR;

    savedState.Restore();
    glDeleteFramebuffers(1, &sourceFbo);
    glDeleteFramebuffers(1, &statsFbo);
    glDeleteTextures(1, &statsTexture);
    if (!readbackOk) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        return;
    }

    std::vector<float> lumas;
    lumas.reserve(static_cast<std::size_t>(statsWidth * statsHeight));
    std::vector<float> lumGrid(static_cast<std::size_t>(statsWidth * statsHeight), 0.0f);
    float clipped = 0.0f;
    float saturated = 0.0f;
    for (int y = 0; y < statsHeight; ++y) {
        for (int x = 0; x < statsWidth; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * statsWidth + x);
            const std::size_t pixelIndex = idx * 4u;
            const float r = std::max(0.0f, pixels[pixelIndex + 0]);
            const float g = std::max(0.0f, pixels[pixelIndex + 1]);
            const float b = std::max(0.0f, pixels[pixelIndex + 2]);
            const float maxChannel = std::max(r, std::max(g, b));
            const float minChannel = std::min(r, std::min(g, b));
            const float saturation = maxChannel > 0.00003f ? (maxChannel - minChannel) / maxChannel : 0.0f;
            const float lum = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
            lumGrid[idx] = lum;
            if (!std::isfinite(lum) || lum <= 0.0f) {
                continue;
            }
            lumas.push_back(lum);
            clipped += maxChannel > 1.0f ? 1.0f : 0.0f;
            saturated += (maxChannel > 0.35f && saturation > 0.58f) ? 1.0f : 0.0f;
        }
    }

    if (lumas.size() < 16) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        return;
    }

    float textureSum = 0.0f;
    float darkTextureSum = 0.0f;
    float darkCount = 0.0f;
    for (int y = 0; y < statsHeight; ++y) {
        for (int x = 0; x < statsWidth; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * statsWidth + x);
            const int left = std::max(0, x - 1);
            const int right = std::min(statsWidth - 1, x + 1);
            const int up = std::max(0, y - 1);
            const int down = std::min(statsHeight - 1, y + 1);
            const float centerLum = std::max(0.000001f, lumGrid[idx]);
            const float gx = std::abs(SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(y * statsWidth + right)])) -
                                      SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(y * statsWidth + left)])));
            const float gy = std::abs(SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(down * statsWidth + x)])) -
                                      SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(up * statsWidth + x)])));
            const float textureProxy = Clamp01((gx + gy) * 0.5f);
            textureSum += textureProxy;
            if (centerLum < 0.08f) {
                darkTextureSum += textureProxy;
                darkCount += 1.0f;
            }
        }
    }

    std::sort(lumas.begin(), lumas.end());
    const float count = static_cast<float>(lumas.size());
    m_AutoSceneShadowPercentile = std::max(PercentileFromSorted(lumas, 0.10f), 0.00001f);
    m_AutoSceneMidtonePercentile = std::max(PercentileFromSorted(lumas, 0.50f), 0.00001f);
    m_AutoSceneHighlightPercentile = std::max(PercentileFromSorted(lumas, 0.98f), 0.00001f);
    const float p01 = std::max(PercentileFromSorted(lumas, 0.01f), 0.000001f);
    const float p05 = std::max(PercentileFromSorted(lumas, 0.05f), 0.000001f);
    const float p25 = std::max(PercentileFromSorted(lumas, 0.25f), 0.000001f);
    const float p40 = std::max(PercentileFromSorted(lumas, 0.40f), 0.000001f);
    const float p60 = std::max(PercentileFromSorted(lumas, 0.60f), 0.000001f);
    const float p75 = std::max(PercentileFromSorted(lumas, 0.75f), 0.000001f);
    const float p90 = std::max(PercentileFromSorted(lumas, 0.90f), 0.000001f);
    m_AutoSceneClippingRatio = Clamp01(clipped / count);
    const float saturationRatio = Clamp01(saturated / count);
    m_AutoSceneTextureConfidence = Clamp01(textureSum / static_cast<float>(statsWidth * statsHeight));
    const float darkTexture = darkCount > 0.0f ? darkTextureSum / darkCount : m_AutoSceneTextureConfidence;
    const float estimatedNoiseFloor = std::clamp(
        std::max(p01, p05 * 0.52f) * (1.0f + darkTexture * 2.85f),
        0.00003f,
        0.10f);

    m_AutoSceneNoiseRisk = Clamp01((estimatedNoiseFloor * 7.0f) / std::max(m_AutoSceneShadowPercentile, 0.00003f));
    const float noisySaturationPressureGuard =
        Clamp01((m_AutoSceneNoiseRisk - 0.55f) / 0.40f);
    const float noisySceneLinearClipGuard =
        Clamp01((m_AutoSceneNoiseRisk - 0.55f) / 0.40f) *
        Clamp01((0.20f - m_AutoSceneClippingRatio) / 0.20f);
    const float clippingHighlightPressure =
        m_AutoSceneClippingRatio * 8.0f * (1.0f - 0.85f * noisySceneLinearClipGuard);
    const float saturationHighlightPressure =
        saturationRatio * 2.5f * (1.0f - 0.70f * noisySaturationPressureGuard);
    m_AutoSceneHighlightPressure = Clamp01(
        std::max(clippingHighlightPressure, saturationHighlightPressure) +
        std::max(0.0f, SafeLog2(m_AutoSceneHighlightPercentile / 0.85f)) * 0.30f);
    m_AutoSceneHdrSpreadEv = std::clamp(
        SafeLog2(std::max(m_AutoSceneHighlightPercentile, 0.0001f) / std::max(m_AutoSceneShadowPercentile, 0.0005f)),
        0.0f,
        16.0f);
    const float sceneKey = std::exp2(
        SafeLog2(p25) * 0.10f +
        SafeLog2(p40) * 0.25f +
        SafeLog2(m_AutoSceneMidtonePercentile) * 0.35f +
        SafeLog2(p60) * 0.20f +
        SafeLog2(p75) * 0.10f);

    const float shadowTarget = LerpFloat(0.18f, 0.32f, 1.0f - m_AutoSceneNoiseRisk);
    float shadowLiftEv = SafeLog2(shadowTarget / std::max(m_AutoSceneShadowPercentile, estimatedNoiseFloor * 8.0f));
    shadowLiftEv *= LerpFloat(0.95f, 0.50f, m_AutoSceneNoiseRisk);
    shadowLiftEv = std::clamp(shadowLiftEv, 0.0f, 2.8f);

    const float broadHighlight = std::max(p90, std::sqrt(std::max(p75 * m_AutoSceneHighlightPercentile, 0.000001f)));
    const float highlightTarget = LerpFloat(0.84f, 0.60f, m_AutoSceneHighlightPressure);
    const float highlightCompressEv = std::clamp(
        SafeLog2(highlightTarget / std::max(broadHighlight, 0.0001f)) - m_AutoSceneHighlightPressure * 0.22f,
        -3.2f,
        0.0f);

    const float lowKeyBias = Clamp01(SafeLog2(0.23f / std::max(sceneKey, 0.000001f)) / 1.6f);
    const float highKeyBias = Clamp01(SafeLog2(std::max(sceneKey, 0.000001f) / 0.34f) / 1.4f);
    const float midtoneTarget = std::clamp(
        0.29f +
            lowKeyBias * 0.06f -
            highKeyBias * 0.05f -
            m_AutoSceneHighlightPressure * 0.05f -
            m_AutoSceneNoiseRisk * 0.04f,
        0.23f,
        0.40f);
    float baseEv = SafeLog2(midtoneTarget / std::max(sceneKey, 0.000001f));
    baseEv *= LerpFloat(1.00f, 0.82f, std::max(m_AutoSceneHighlightPressure, m_AutoSceneNoiseRisk));
    baseEv = std::clamp(baseEv, -1.3f, 1.3f);

    const float shadowPressure = Clamp01(shadowLiftEv / 2.5f);
    const float keyMatch = 1.0f - Clamp01(std::abs(SafeLog2(std::max(sceneKey, 0.000001f) / std::max(midtoneTarget, 0.000001f))) / 0.80f);
    const float highlightStability =
        1.0f - Clamp01(std::max(0.0f, m_AutoSceneHighlightPressure - 0.34f) * 1.35f + m_AutoSceneClippingRatio * 5.0f);
    const float wellExposedStability = Clamp01(
        keyMatch *
        (1.0f - shadowPressure * 0.72f) *
        (1.0f - m_AutoSceneNoiseRisk * 0.35f) *
        (0.55f + highlightStability * 0.45f));
    baseEv *= 1.0f - 0.45f * wellExposedStability;
    if (m_AutoSceneNoiseRisk > 0.72f && m_AutoSceneShadowPercentile < 0.08f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::NoisyLowLight;
    } else if (m_AutoSceneHdrSpreadEv < 2.2f && m_AutoSceneHighlightPressure < 0.22f && m_AutoSceneNoiseRisk < 0.35f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::Flat;
    } else if (m_AutoSceneHighlightPressure > 0.58f && m_AutoSceneHdrSpreadEv > 3.4f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::HighlightHeavy;
    } else if (shadowPressure > 0.55f && m_AutoSceneHighlightPressure < 0.45f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::ShadowHeavy;
    } else {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::Balanced;
    }
    if (baseEv < 0.0f) {
        float negativeBaseEvPermission = std::clamp(
            m_AutoSceneHighlightPressure * 0.75f +
            m_AutoSceneClippingRatio * 5.5f +
            shadowPressure * 0.18f,
            0.12f,
            1.0f);
        if (m_AutoSceneProfile == ToneCurveAutoSceneProfile::Flat ||
            m_AutoSceneProfile == ToneCurveAutoSceneProfile::Balanced) {
            negativeBaseEvPermission *= 0.65f;
        }
        negativeBaseEvPermission *= 1.0f - 0.25f * wellExposedStability;
        baseEv *= negativeBaseEvPermission;
    }
    m_AutoRecommendedBaseEv = baseEv;

    float localStrengthBias = 0.0f;
    float shadowOpeningBias = 0.0f;
    float highlightCompressionBias = 0.0f;
    float baseEvBias = 0.0f;
    float foundationScale = 1.0f;
    float shadowsEvBias = 0.0f;
    float darksEvBias = 0.0f;
    float midtonesEvBias = 0.0f;
    float lightsEvBias = 0.0f;
    float highlightsEvBias = 0.0f;
    switch (m_AutoSceneProfile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy:
            localStrengthBias = 0.04f;
            shadowOpeningBias = 0.06f;
            highlightCompressionBias = 0.20f;
            baseEvBias = -0.12f;
            shadowsEvBias = 0.08f;
            darksEvBias = 0.08f;
            lightsEvBias = -0.14f;
            highlightsEvBias = -0.22f;
            break;
        case ToneCurveAutoSceneProfile::ShadowHeavy:
            localStrengthBias = 0.05f;
            shadowOpeningBias = 0.18f;
            highlightCompressionBias = -0.04f;
            baseEvBias = 0.10f;
            shadowsEvBias = 0.18f;
            darksEvBias = 0.14f;
            midtonesEvBias = 0.06f;
            break;
        case ToneCurveAutoSceneProfile::Flat:
            localStrengthBias = -0.18f;
            shadowOpeningBias = -0.14f;
            highlightCompressionBias = -0.14f;
            baseEvBias *= 0.0f;
            foundationScale = 0.52f;
            break;
        case ToneCurveAutoSceneProfile::NoisyLowLight:
            localStrengthBias = -0.16f;
            shadowOpeningBias = -0.14f;
            highlightCompressionBias = 0.04f;
            baseEvBias = 0.04f;
            foundationScale = 0.68f;
            shadowsEvBias = -0.08f;
            darksEvBias = -0.04f;
            break;
        case ToneCurveAutoSceneProfile::Balanced:
        default:
            break;
    }

    m_AutoRecommendedLocalStrength = std::clamp(
        0.82f + shadowPressure * 0.28f + m_AutoSceneHighlightPressure * 0.24f + localStrengthBias - (1.0f - m_AutoSceneTextureConfidence) * 0.10f,
        0.45f,
        1.45f);
    m_AutoRecommendedShadowOpening = std::clamp(
        0.82f + shadowPressure * 0.78f + m_AutoSceneHighlightPressure * 0.22f + shadowOpeningBias - m_AutoSceneNoiseRisk * 0.12f,
        0.45f,
        2.2f);
    m_AutoRecommendedHighlightCompression = std::clamp(
        0.86f + m_AutoSceneHighlightPressure * 0.88f + shadowPressure * 0.16f + highlightCompressionBias,
        0.45f,
        2.2f);

    baseEv = std::clamp(baseEv + baseEvBias, -1.5f, 1.5f);
    m_AutoRecommendedFoundationEv[0] = std::clamp((shadowLiftEv * 0.72f + shadowsEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[1] = std::clamp((shadowLiftEv * 0.48f + baseEv * 0.40f + darksEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[2] = std::clamp((baseEv * 0.72f + midtonesEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[3] = std::clamp((highlightCompressEv * 0.40f + baseEv * 0.18f + lightsEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[4] = std::clamp((highlightCompressEv * 0.78f + highlightsEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoSceneStatsValid = true;
    m_AutoSceneStats.valid = true;
    m_AutoSceneStats.shadowPercentile = m_AutoSceneShadowPercentile;
    m_AutoSceneStats.midtonePercentile = m_AutoSceneMidtonePercentile;
    m_AutoSceneStats.highlightPercentile = m_AutoSceneHighlightPercentile;
    m_AutoSceneStats.clippingRatio = m_AutoSceneClippingRatio;
    m_AutoSceneStats.noiseRisk = m_AutoSceneNoiseRisk;
    m_AutoSceneStats.highlightPressure = m_AutoSceneHighlightPressure;
    m_AutoSceneStats.textureConfidence = m_AutoSceneTextureConfidence;
    m_AutoSceneStats.hdrSpreadEv = m_AutoSceneHdrSpreadEv;
    m_AutoSceneStats.profile = m_AutoSceneProfile;
    m_AutoSceneStats.recommendedBaseEv = m_AutoRecommendedBaseEv;
    m_AutoSceneStats.recommendedLocalStrength = m_AutoRecommendedLocalStrength;
    m_AutoSceneStats.recommendedShadowOpening = m_AutoRecommendedShadowOpening;
    m_AutoSceneStats.recommendedHighlightCompression = m_AutoRecommendedHighlightCompression;
    m_AutoSceneStats.recommendedFoundationEv = m_AutoRecommendedFoundationEv;
    m_AutoSceneAnalysisTexture = inputTexture;
    m_AutoSceneAnalysisWidth = width;
    m_AutoSceneAnalysisHeight = height;
    m_AutoSceneAnalysisFramesUntilRefresh = 7;
}

ToneCurveLayer::AutoToneIntent ToneCurveLayer::SolveAutoToneIntent() const {
    AutoToneIntent intent;
    const AutoSceneStats& stats = m_AutoSceneStats;
    if (!stats.valid) {
        intent.localBaseline = ComputeEffectiveLocalBaselineSettings();
        intent.localBaselineEnabled = m_LocalBaselineEnabled;
        intent.middleGrey = std::clamp(m_MiddleGrey, 0.01f, 1.0f);
        intent.logMinEv = m_LogMinEv;
        intent.logMaxEv = m_LogMaxEv;
        intent.targetAffectWidth = std::clamp(m_TargetAffectWidth, 0.02f, 0.30f);
        intent.targetShadowProtection = std::clamp(m_TargetShadowProtection, 0.0f, 1.0f);
        intent.targetHighlightProtection = std::clamp(m_TargetHighlightProtection, 0.0f, 1.0f);
        intent.foundationAdaptiveAssist = m_FoundationAdaptiveAssist;
        intent.foundationAssistStrength = std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f);
        intent.foundationBandWidth = std::clamp(m_FoundationBandWidth, 0.5f, 8.0f);
        intent.foundationPreserveHue = m_FoundationPreserveHue;
        intent.foundationRegionEv = GetFoundationRegionValues();
        return intent;
    }

    EffectiveLocalBaselineSettings canonicalLocalBaseline;
    canonicalLocalBaseline.strength = 0.0f;
    canonicalLocalBaseline.shadowOpening = 0.0f;
    canonicalLocalBaseline.highlightCompression = 0.0f;
    canonicalLocalBaseline.radius = 72.0f;
    canonicalLocalBaseline.edgeProtection = 0.65f;
    canonicalLocalBaseline.rangeProtection = 0.45f;
    constexpr float kCanonicalMiddleGrey = 0.18f;
    constexpr float kCanonicalFoundationBandWidth = 2.50f;
    constexpr float kCanonicalTargetAffectWidth = 0.08f;
    constexpr float kCanonicalTargetProtection = 0.65f;
    constexpr std::array<float, 5> kCanonicalFoundationRegionEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    const float autoAssist = std::clamp(m_AutoSceneAssistStrength, 0.0f, 2.4f);
    const float baseBlend = std::clamp(autoAssist, 0.0f, 1.0f);
    const float extraAutoBoost = std::max(0.0f, autoAssist - 1.0f);
    const float autoBiasScale = baseBlend + extraAutoBoost * 0.55f;
    const float dynamicRangeControl = std::clamp(m_AutoDynamicRange, 0.25f, 3.0f);
    const float autoTuningPresence = std::clamp(baseBlend + extraAutoBoost * 0.45f, 0.0f, 1.65f);
    const float rawDynamicRangeDelta = dynamicRangeControl - 1.0f;
    const float dynamicRangeDelta =
        std::copysign(std::pow(std::abs(rawDynamicRangeDelta), 0.92f), rawDynamicRangeDelta) * autoTuningPresence;
    const float shadowBias = std::clamp(m_AutoShadowBias, -1.25f, 1.25f) * autoTuningPresence;
    const float highlightBias = std::clamp(m_AutoHighlightBias, -1.25f, 1.25f) * autoTuningPresence;
    const float highlightCharacter = std::clamp(m_AutoHighlightCharacter, -1.25f, 1.25f) * autoTuningPresence;
    const float contrastBias = std::clamp(m_AutoContrastBias, -1.25f, 1.25f) * autoTuningPresence;
    const float liftNeed = Clamp01(std::max(0.0f, stats.recommendedBaseEv) / 1.25f);
    const float highlightGuard = std::clamp(1.0f - stats.highlightPressure * 0.55f, 0.30f, 1.0f);
    const float noiseGuard = std::clamp(1.0f - stats.noiseRisk * 0.60f, 0.25f, 1.0f);
    const float positiveHighlightCharacter = std::max(0.0f, highlightCharacter);
    const float negativeHighlightCharacter = std::max(0.0f, -highlightCharacter);
    intent.localBaseline.strength = std::clamp(
        LerpFloat(canonicalLocalBaseline.strength, stats.recommendedLocalStrength, baseBlend),
        0.0f,
        1.6f);
    intent.localBaseline.shadowOpening = std::clamp(
        LerpFloat(canonicalLocalBaseline.shadowOpening, stats.recommendedShadowOpening, baseBlend),
        0.0f,
        2.2f);
    intent.localBaseline.highlightCompression = std::clamp(
        LerpFloat(canonicalLocalBaseline.highlightCompression, stats.recommendedHighlightCompression, baseBlend),
        0.0f,
        2.2f);

    const float textureComplexity = std::clamp(stats.textureConfidence, 0.0f, 1.0f);
    const float flatness = 1.0f - textureComplexity;
    const float shadowPressure = Clamp01(
        (std::max(0.0f, stats.recommendedFoundationEv[0]) +
         std::max(0.0f, stats.recommendedFoundationEv[1]) * 0.70f +
         std::max(0.0f, stats.recommendedBaseEv) * 1.05f) / 2.55f);
    const float brightDetailPressure = Clamp01(stats.highlightPressure * 0.78f + std::clamp(stats.clippingRatio * 22.0f, 0.0f, 1.0f) * 0.22f);
    const float underBrightBroadHighlight = Clamp01(
        SafeLog2(0.68f / std::max(stats.highlightPercentile, 0.0001f)) / 1.45f) *
        Clamp01((0.55f - stats.highlightPressure) / 0.55f) *
        Clamp01((stats.hdrSpreadEv - 3.2f) / 2.0f) *
        Clamp01((stats.midtonePercentile - 0.08f) / 0.12f) *
        (1.0f - stats.noiseRisk * 0.45f);
    const float sceneContrastRestore = underBrightBroadHighlight * autoTuningPresence;
    const float highlightPunchNeed = Clamp01((shadowPressure * 0.72f + liftNeed * 0.28f) * brightDetailPressure);
    const float stableExposureGuard = Clamp01(
        (1.0f - Clamp01(std::abs(stats.recommendedBaseEv) / 0.60f)) *
        (1.0f - shadowPressure * 0.85f) *
        (1.0f - stats.noiseRisk * 0.35f) *
        (1.0f - std::max(0.0f, stats.highlightPressure - 0.35f) * 1.20f) *
        (1.0f - stats.clippingRatio * 4.0f));
    const float neutralHighlightGuard = stableExposureGuard * (1.0f - brightDetailPressure);
    const float stableStrengthNeutrality =
        stableExposureGuard * Clamp01((autoAssist - 0.55f) / 1.25f);
    const float noisyLowLightToneOverlapGuard =
        stats.profile == ToneCurveAutoSceneProfile::NoisyLowLight
            ? Clamp01((stats.noiseRisk - 0.48f) / 0.42f) *
                Clamp01((0.34f - stats.highlightPressure) / 0.34f) *
                Clamp01((stats.hdrSpreadEv - 2.2f) / 2.4f) *
                (0.65f + 0.35f * liftNeed) *
                autoTuningPresence
            : 0.0f;
    const float scenePrepLiftToneBudgetGuard =
        m_DevelopScenePrepToneBudgetActive && stats.profile == ToneCurveAutoSceneProfile::NoisyLowLight
            ? Clamp01((m_DevelopScenePrepToneBudgetMaxEvBias - 0.18f) / 0.72f) *
                Clamp01((m_DevelopScenePrepToneBudgetStrength - 0.48f) / 0.36f) *
                Clamp01((stats.noiseRisk - 0.48f) / 0.38f) *
                Clamp01((stats.hdrSpreadEv - 2.0f) / 1.8f) *
                (0.70f + 0.30f * liftNeed) *
                autoTuningPresence
            : 0.0f;
    const float scenePrepToneOverlapGuard =
        std::max(noisyLowLightToneOverlapGuard, scenePrepLiftToneBudgetGuard);
    const float flattenRisk = std::clamp(
        flatness * 0.34f +
        stats.noiseRisk * 0.42f +
        std::clamp(0.55f - stats.highlightPressure, 0.0f, 0.55f) * 0.18f,
        0.0f,
        1.0f);
    intent.localBaseline.strength = std::clamp(
        intent.localBaseline.strength * (1.0f - 0.22f * flattenRisk),
        0.0f,
        1.6f);
    if (sceneContrastRestore > 0.0001f) {
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength * (1.0f - 0.10f * sceneContrastRestore),
            0.0f,
            1.6f);
    }
    intent.localBaseline.shadowOpening = std::clamp(
        intent.localBaseline.shadowOpening * (1.0f - 0.12f * stats.noiseRisk),
        0.0f,
        2.2f);
    intent.localBaseline.highlightCompression = std::clamp(
        intent.localBaseline.highlightCompression * (1.0f - 0.08f * flatness) + stats.highlightPressure * 0.08f * autoAssist,
        0.0f,
        2.2f);
    if (highlightPunchNeed > 0.0001f) {
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + highlightPunchNeed * (0.08f + 0.10f * brightDetailPressure),
            0.0f,
            2.2f);
    }
    intent.localBaseline.radius = std::clamp(
        LerpFloat(
            canonicalLocalBaseline.radius,
            std::clamp(
                104.0f - textureComplexity * 40.0f + stats.highlightPressure * 20.0f + stats.noiseRisk * 14.0f,
                28.0f,
                180.0f),
            baseBlend),
        8.0f,
        220.0f);
    intent.localBaseline.edgeProtection = std::clamp(
        LerpFloat(
            canonicalLocalBaseline.edgeProtection,
            std::clamp(
                0.48f + flatness * 0.18f + stats.highlightPressure * 0.12f + stats.noiseRisk * 0.10f,
                0.15f,
                1.0f),
            baseBlend),
        0.0f,
        1.0f);
    intent.localBaseline.rangeProtection = std::clamp(
        LerpFloat(
            canonicalLocalBaseline.rangeProtection,
            std::clamp(
                0.34f + stats.noiseRisk * 0.42f + stats.highlightPressure * 0.16f + flatness * 0.08f,
                0.10f,
                1.0f),
            baseBlend),
        0.0f,
        1.0f);
    if (highlightPunchNeed > 0.0001f) {
        intent.localBaseline.edgeProtection = std::clamp(
            intent.localBaseline.edgeProtection + highlightPunchNeed * (0.14f + 0.10f * brightDetailPressure),
            0.0f,
            1.0f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + highlightPunchNeed * (0.08f + 0.08f * brightDetailPressure),
            0.0f,
            1.0f);
    }
    intent.localBaselineEnabled =
        intent.localBaseline.strength > 0.001f ||
        intent.localBaseline.shadowOpening > 0.001f ||
        intent.localBaseline.highlightCompression > 0.001f;

    if (extraAutoBoost > 0.0001f) {
        const float shadowLiftBoost = extraAutoBoost * liftNeed * highlightGuard * noiseGuard;
        intent.localBaselineEnabled = true;
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength + shadowLiftBoost * (0.12f + shadowPressure * 0.22f),
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening + shadowLiftBoost * (0.22f + shadowPressure * 0.34f),
            0.0f,
            2.2f);
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + extraAutoBoost * stats.highlightPressure * 0.08f,
            0.0f,
            2.2f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + extraAutoBoost * 0.06f,
            0.0f,
            1.0f);
    }

    if (std::abs(shadowBias) > 0.0001f) {
        const float positiveShadowBias = std::max(0.0f, shadowBias);
        const float negativeShadowBias = std::max(0.0f, -shadowBias);
        intent.localBaselineEnabled = true;
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength + positiveShadowBias * 0.10f * noiseGuard - negativeShadowBias * 0.08f,
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening + positiveShadowBias * (0.22f + 0.12f * liftNeed) * noiseGuard -
                negativeShadowBias * 0.18f,
            0.0f,
            2.2f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + positiveShadowBias * 0.05f,
            0.0f,
            1.0f);
    }
    if (std::abs(highlightBias) > 0.0001f) {
        const float positiveHighlightBias = std::max(0.0f, highlightBias);
        const float negativeHighlightBias = std::max(0.0f, -highlightBias);
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + positiveHighlightBias * (0.18f + stats.highlightPressure * 0.14f) -
                negativeHighlightBias * 0.16f,
            0.0f,
            2.2f);
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection + positiveHighlightBias * 0.08f - negativeHighlightBias * 0.06f,
            0.0f,
            1.0f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression - positiveHighlightCharacter * (0.05f + 0.05f * brightDetailPressure),
            0.0f,
            2.2f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection - positiveHighlightCharacter * 0.04f,
            0.0f,
            1.0f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + negativeHighlightCharacter * (0.09f + 0.09f * brightDetailPressure),
            0.0f,
            2.2f);
        intent.localBaseline.edgeProtection = std::clamp(
            intent.localBaseline.edgeProtection + negativeHighlightCharacter * 0.05f,
            0.0f,
            1.0f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + negativeHighlightCharacter * 0.05f,
            0.0f,
            1.0f);
    }

    float anchorEvBias = std::clamp(stats.recommendedBaseEv, -1.25f, 1.25f) *
        (0.72f + 0.16f * baseBlend + 0.18f * extraAutoBoost * liftNeed);
    switch (stats.profile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy:
            anchorEvBias -= 0.16f * autoBiasScale * (1.0f - 0.60f * stableExposureGuard);
            break;
        case ToneCurveAutoSceneProfile::ShadowHeavy:
            anchorEvBias += 0.12f * autoBiasScale;
            break;
        case ToneCurveAutoSceneProfile::Flat:
            anchorEvBias *= 0.72f;
            break;
        case ToneCurveAutoSceneProfile::NoisyLowLight:
            anchorEvBias += 0.08f * autoBiasScale;
            break;
        case ToneCurveAutoSceneProfile::Balanced:
        default:
            break;
    }
    const float highlightRestraint = 1.0f - 0.10f * stats.highlightPressure * (1.0f - 0.55f * stableExposureGuard);
    const float noiseLiftGuard = 1.0f - 0.06f * stats.noiseRisk;
    intent.middleGrey = std::clamp(
        kCanonicalMiddleGrey * std::exp2(anchorEvBias) * highlightRestraint * noiseLiftGuard,
        0.02f,
        0.50f);
    if (extraAutoBoost > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.24f * extraAutoBoost * liftNeed * highlightGuard * noiseGuard),
            0.02f,
            0.50f);
    }
    if (shadowBias > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.08f * shadowBias * liftNeed * noiseGuard),
            0.02f,
            0.50f);
    } else if (shadowBias < -0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.05f * shadowBias),
            0.02f,
            0.50f);
    }
    if (highlightBias > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(-0.04f * highlightBias),
            0.02f,
            0.50f);
    }
    if (highlightPunchNeed > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(-0.08f * highlightPunchNeed),
            0.02f,
            0.50f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(-0.06f * positiveHighlightCharacter * (0.70f + shadowPressure * 0.30f)),
            0.02f,
            0.50f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.05f * negativeHighlightCharacter * (0.55f + brightDetailPressure * 0.45f)),
            0.02f,
            0.50f);
    }

    float shadowEv = std::clamp(
        SafeLog2(intent.middleGrey / std::max(stats.shadowPercentile, 0.0005f)) + 1.55f + stats.noiseRisk * 0.48f,
        4.5f,
        12.5f);
    float highlightEv = std::clamp(
        SafeLog2(std::max(stats.highlightPercentile, 0.0001f) / intent.middleGrey) + 1.95f + stats.highlightPressure * 0.92f,
        3.5f,
        9.5f);
    shadowEv = std::clamp(
        shadowEv +
            dynamicRangeDelta * (0.85f + liftNeed * 0.55f) * noiseGuard +
            extraAutoBoost * liftNeed * 0.32f * noiseGuard,
        4.5f,
        12.8f);
    highlightEv = std::clamp(
        highlightEv +
            dynamicRangeDelta * (0.78f + stats.highlightPressure * 0.42f) * (0.75f + highlightGuard * 0.25f) +
            extraAutoBoost * liftNeed * 0.18f,
        3.5f,
        10.5f);
    intent.logMinEv = std::clamp(-shadowEv, -14.0f, -2.0f);
    intent.logMaxEv = std::clamp(highlightEv, 2.0f, 10.5f);
    if (intent.logMaxEv - intent.logMinEv < 6.0f) {
        intent.logMaxEv = intent.logMinEv + 6.0f;
    }
    intent.logMinEv = std::clamp(intent.logMinEv - std::max(0.0f, shadowBias) * 0.28f * noiseGuard, -14.5f, -2.0f);
    intent.logMaxEv = std::clamp(intent.logMaxEv + std::max(0.0f, highlightBias) * 0.26f, 2.0f, 10.8f);
    if (highlightPunchNeed > 0.0001f) {
        intent.logMaxEv = std::clamp(intent.logMaxEv + 0.18f * highlightPunchNeed, 2.0f, 10.8f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.logMaxEv = std::clamp(
            intent.logMaxEv - positiveHighlightCharacter * (0.12f + 0.08f * brightDetailPressure),
            2.0f,
            10.8f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.logMaxEv = std::clamp(
            intent.logMaxEv + negativeHighlightCharacter * (0.18f + 0.10f * brightDetailPressure),
            2.0f,
            10.8f);
    }
    if (intent.logMaxEv - intent.logMinEv < 6.0f) {
        intent.logMaxEv = intent.logMinEv + 6.0f;
    }
    if (stableStrengthNeutrality > 0.0001f) {
        intent.logMaxEv = std::clamp(
            intent.logMaxEv + 0.42f * stableStrengthNeutrality,
            2.0f,
            10.8f);
    }

    const float hdrSpread = std::clamp(stats.hdrSpreadEv / 8.0f, 0.0f, 1.0f);
    intent.foundationBandWidth = std::clamp(
        kCanonicalFoundationBandWidth +
            (std::clamp(2.1f + hdrSpread * 1.45f + flatness * 0.42f - stats.noiseRisk * 0.14f, 1.4f, 5.2f) -
             kCanonicalFoundationBandWidth) * baseBlend,
        0.5f,
        8.0f);
    intent.foundationBandWidth = std::clamp(
        intent.foundationBandWidth * (1.0f + dynamicRangeDelta * 0.16f),
        0.5f,
        8.0f);
    intent.targetAffectWidth = std::clamp(
        kCanonicalTargetAffectWidth +
            (std::clamp(
                kCanonicalTargetAffectWidth +
                    std::clamp((intent.foundationBandWidth - kCanonicalFoundationBandWidth) / 3.0f, -1.0f, 1.0f) * 0.05f +
                    stats.highlightPressure * 0.03f + stats.noiseRisk * 0.015f,
                0.035f,
                0.26f) - kCanonicalTargetAffectWidth) * baseBlend,
        0.02f,
        0.30f);
    intent.targetAffectWidth = std::clamp(
        intent.targetAffectWidth * (1.0f - dynamicRangeDelta * 0.10f),
        0.02f,
        0.30f);
    intent.targetShadowProtection = std::clamp(
        kCanonicalTargetProtection +
            (std::clamp(0.58f + stats.noiseRisk * 0.26f + flatness * 0.08f, 0.0f, 1.0f) -
             kCanonicalTargetProtection) * baseBlend,
        0.0f,
        1.0f);
    intent.targetHighlightProtection = std::clamp(
        kCanonicalTargetProtection +
            (std::clamp(0.56f + stats.highlightPressure * 0.28f + stats.clippingRatio * 0.10f, 0.0f, 1.0f) -
             kCanonicalTargetProtection) * baseBlend,
        0.0f,
        1.0f);
    intent.targetShadowProtection = std::clamp(
        intent.targetShadowProtection + extraAutoBoost * 0.05f + std::max(0.0f, -dynamicRangeDelta) * 0.04f,
        0.0f,
        1.0f);
    intent.targetHighlightProtection = std::clamp(
        intent.targetHighlightProtection + extraAutoBoost * 0.08f + std::max(0.0f, dynamicRangeDelta) * 0.06f,
        0.0f,
        1.0f);
    intent.targetShadowProtection = std::clamp(
        intent.targetShadowProtection + std::max(0.0f, shadowBias) * 0.05f,
        0.0f,
        1.0f);
    intent.targetHighlightProtection = std::clamp(
        intent.targetHighlightProtection + std::max(0.0f, highlightBias) * 0.10f - std::max(0.0f, -highlightBias) * 0.08f,
        0.0f,
        1.0f);
    if (positiveHighlightCharacter > 0.0001f) {
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection - positiveHighlightCharacter * 0.12f,
            0.0f,
            1.0f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection + negativeHighlightCharacter * 0.14f,
            0.0f,
            1.0f);
    }
    intent.targetAffectWidth = std::clamp(
        intent.targetAffectWidth * (1.0f - contrastBias * 0.05f),
        0.02f,
        0.30f);
    if (neutralHighlightGuard > 0.0001f) {
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression * (1.0f - 0.12f * neutralHighlightGuard),
            0.0f,
            2.2f);
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection - 0.08f * neutralHighlightGuard,
            0.0f,
            1.0f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection - 0.18f * stableStrengthNeutrality,
            0.0f,
            1.0f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength * (1.0f - 0.35f * stableStrengthNeutrality),
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening * (1.0f - 0.28f * stableStrengthNeutrality),
            0.0f,
            2.2f);
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression * (1.0f - 0.40f * stableStrengthNeutrality),
            0.0f,
            2.2f);
    }
    if (scenePrepToneOverlapGuard > 0.0001f) {
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength * (1.0f - 0.28f * scenePrepToneOverlapGuard),
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening * (1.0f - 0.18f * scenePrepToneOverlapGuard),
            0.0f,
            2.2f);
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression * (1.0f - 0.62f * scenePrepToneOverlapGuard),
            0.0f,
            2.2f);
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection * (1.0f - 0.18f * scenePrepToneOverlapGuard),
            0.0f,
            1.0f);
        intent.targetAffectWidth = std::clamp(
            intent.targetAffectWidth * (1.0f - 0.10f * scenePrepToneOverlapGuard),
            0.02f,
            0.30f);
    }

    intent.foundationAdaptiveAssist = false;
    intent.foundationAssistStrength = 0.0f;
    intent.foundationPreserveHue = true;
    std::array<float, 5> authoredFoundation = kCanonicalFoundationRegionEv;
    for (int i = 0; i < 5; ++i) {
        authoredFoundation[static_cast<std::size_t>(i)] =
            std::clamp(
                LerpFloat(
                    authoredFoundation[static_cast<std::size_t>(i)],
                    stats.recommendedFoundationEv[static_cast<std::size_t>(i)],
                    baseBlend),
                -5.0f,
                5.0f);
    }

    const float assistBlend = std::clamp(0.35f + stats.highlightPressure * 0.18f + flatness * 0.12f, 0.0f, 0.72f) *
        (baseBlend + extraAutoBoost * 0.25f);
    if (assistBlend > 0.0001f) {
        std::array<float, 5> smoothed = authoredFoundation;
        const float sigma = std::clamp(0.95f + intent.foundationBandWidth * 0.16f, 0.95f, 1.80f);
        for (int i = 0; i < 5; ++i) {
            float weightedSum = 0.0f;
            float weightTotal = 0.0f;
            for (int j = 0; j < 5; ++j) {
                const float d = static_cast<float>(i - j);
                const float weight = std::exp(-0.5f * (d * d) / std::max(0.05f, sigma * sigma));
                weightedSum += authoredFoundation[static_cast<std::size_t>(j)] * weight;
                weightTotal += weight;
            }
            smoothed[static_cast<std::size_t>(i)] = weightedSum / std::max(0.0001f, weightTotal);
        }
        for (int i = 0; i < 5; ++i) {
            authoredFoundation[static_cast<std::size_t>(i)] = std::clamp(
                LerpFloat(authoredFoundation[static_cast<std::size_t>(i)], smoothed[static_cast<std::size_t>(i)], assistBlend),
                -5.0f,
                5.0f);
        }
    }
    if (dynamicRangeDelta > 0.0001f) {
        const float liftRollback = dynamicRangeDelta * (0.20f + liftNeed * 0.30f);
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - liftRollback * (0.70f + stats.noiseRisk * 0.15f), -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - liftRollback * 0.48f, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] - dynamicRangeDelta * liftNeed * 0.06f, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + dynamicRangeDelta * 0.04f * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + dynamicRangeDelta * 0.08f * highlightGuard, -5.0f, 5.0f);
    } else if (dynamicRangeDelta < -0.0001f) {
        const float flatter = -dynamicRangeDelta;
        authoredFoundation[0] = std::clamp(authoredFoundation[0] + flatter * 0.18f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] + flatter * 0.12f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - flatter * 0.06f, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - flatter * 0.10f, -5.0f, 5.0f);
    }
    if (extraAutoBoost > 0.0001f) {
        const float shadowRecover = extraAutoBoost * liftNeed * highlightGuard * noiseGuard;
        authoredFoundation[0] = std::clamp(authoredFoundation[0] + 0.22f * shadowRecover, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] + 0.16f * shadowRecover, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] + 0.08f * shadowRecover, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - 0.02f * extraAutoBoost * stats.highlightPressure, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - 0.05f * extraAutoBoost * stats.highlightPressure, -5.0f, 5.0f);
    }
    if (std::abs(shadowBias) > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] + shadowBias * 0.24f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] + shadowBias * 0.16f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] + shadowBias * 0.05f * liftNeed, -5.0f, 5.0f);
    }
    if (std::abs(highlightBias) > 0.0001f) {
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - highlightBias * 0.10f, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - highlightBias * 0.22f, -5.0f, 5.0f);
    }
    if (highlightPunchNeed > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - 0.10f * highlightPunchNeed * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - 0.06f * highlightPunchNeed * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.05f * highlightPunchNeed * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.10f * highlightPunchNeed * highlightGuard, -5.0f, 5.0f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - 0.05f * positiveHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - 0.03f * positiveHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.08f * positiveHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.18f * positiveHighlightCharacter, -5.0f, 5.0f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - 0.06f * negativeHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - 0.14f * negativeHighlightCharacter, -5.0f, 5.0f);
    }
    if (std::abs(contrastBias) > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - contrastBias * 0.16f, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - contrastBias * 0.12f, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + contrastBias * 0.08f * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + contrastBias * 0.16f * highlightGuard, -5.0f, 5.0f);
        intent.foundationBandWidth = std::clamp(intent.foundationBandWidth * (1.0f - contrastBias * 0.06f), 0.5f, 8.0f);
    }
    if (sceneContrastRestore > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - sceneContrastRestore * 0.10f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - sceneContrastRestore * 0.06f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + sceneContrastRestore * 0.04f * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + sceneContrastRestore * 0.08f * highlightGuard, -5.0f, 5.0f);
        intent.foundationBandWidth = std::clamp(intent.foundationBandWidth * (1.0f - sceneContrastRestore * 0.03f), 0.5f, 8.0f);
    }
    if (neutralHighlightGuard > 0.0001f) {
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.03f * neutralHighlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.06f * neutralHighlightGuard, -5.0f, 5.0f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        for (float& value : authoredFoundation) {
            value = LerpFloat(value, 0.0f, 0.50f * stableStrengthNeutrality);
        }
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.08f * stableStrengthNeutrality, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.14f * stableStrengthNeutrality, -5.0f, 5.0f);
    }
    if (scenePrepToneOverlapGuard > 0.0001f) {
        const float lowLightContrast = scenePrepToneOverlapGuard * (0.65f + 0.35f * liftNeed);
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - 0.18f * lowLightContrast, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - 0.10f * lowLightContrast, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] + 0.04f * lowLightContrast, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(
            std::max(authoredFoundation[3] + 0.10f * lowLightContrast, -0.06f * (1.0f - scenePrepToneOverlapGuard)),
            -5.0f,
            5.0f);
        authoredFoundation[4] = std::clamp(
            std::max(authoredFoundation[4] + 0.18f * lowLightContrast, -0.08f * (1.0f - scenePrepToneOverlapGuard)),
            -5.0f,
            5.0f);
    }
    intent.foundationRegionEv = authoredFoundation;

    std::array<float, 5> residualProfile { 0.08f, 0.04f, 0.00f, -0.04f, -0.10f };
    switch (stats.profile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy:
            residualProfile = { 0.10f, 0.06f, -0.01f, -0.10f, -0.22f };
            break;
        case ToneCurveAutoSceneProfile::ShadowHeavy:
            residualProfile = { 0.14f, 0.08f, 0.02f, -0.03f, -0.12f };
            break;
        case ToneCurveAutoSceneProfile::Flat:
            residualProfile = { 0.04f, 0.02f, 0.00f, -0.02f, -0.05f };
            break;
        case ToneCurveAutoSceneProfile::NoisyLowLight:
            residualProfile = { 0.02f, 0.01f, 0.00f, -0.03f, -0.08f };
            break;
        case ToneCurveAutoSceneProfile::Balanced:
        default:
            break;
    }
    const float residualScale = std::clamp(
        (0.35f + stats.highlightPressure * 0.30f + (1.0f - stats.noiseRisk) * 0.10f) *
            (0.78f + dynamicRangeControl * 0.22f) +
            extraAutoBoost * 0.04f,
        0.16f,
        0.92f) * (baseBlend + extraAutoBoost * 0.35f);
    for (int i = 0; i < 5; ++i) {
        intent.pointResidualEv[static_cast<std::size_t>(i)] =
            std::clamp(residualProfile[static_cast<std::size_t>(i)] * residualScale, -0.28f, 0.22f);
    }
    if (dynamicRangeDelta > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - 0.10f * dynamicRangeDelta, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - 0.06f * dynamicRangeDelta, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.05f * dynamicRangeDelta, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.10f * dynamicRangeDelta, -0.28f, 0.22f);
    } else if (dynamicRangeDelta < -0.0001f) {
        const float flatter = -dynamicRangeDelta;
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + 0.06f * flatter * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + 0.04f * flatter * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.04f * flatter, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.07f * flatter, -0.28f, 0.22f);
    }
    if (extraAutoBoost > 0.0001f) {
        const float shadowCurveBoost = extraAutoBoost * liftNeed * noiseGuard;
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + 0.04f * shadowCurveBoost, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + 0.03f * shadowCurveBoost, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.02f * extraAutoBoost * stats.highlightPressure, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.03f * extraAutoBoost * stats.highlightPressure, -0.28f, 0.22f);
    }
    if (std::abs(shadowBias) > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + shadowBias * 0.05f * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + shadowBias * 0.03f * noiseGuard, -0.28f, 0.22f);
    }
    if (std::abs(highlightBias) > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - highlightBias * 0.04f, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - highlightBias * 0.08f, -0.28f, 0.22f);
    }
    if (highlightPunchNeed > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.02f * highlightPunchNeed, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.04f * highlightPunchNeed, -0.28f, 0.22f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.04f * positiveHighlightCharacter, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.08f * positiveHighlightCharacter, -0.28f, 0.22f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.03f * negativeHighlightCharacter, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.06f * negativeHighlightCharacter, -0.28f, 0.22f);
    }
    if (std::abs(contrastBias) > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - contrastBias * 0.05f, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - contrastBias * 0.03f, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + contrastBias * 0.03f, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + contrastBias * 0.06f, -0.28f, 0.22f);
    }
    if (sceneContrastRestore > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - sceneContrastRestore * 0.030f * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - sceneContrastRestore * 0.020f * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + sceneContrastRestore * 0.020f * highlightGuard, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + sceneContrastRestore * 0.040f * highlightGuard, -0.28f, 0.22f);
    }
    if (neutralHighlightGuard > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.01f * neutralHighlightGuard, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.02f * neutralHighlightGuard, -0.28f, 0.22f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        for (float& residual : intent.pointResidualEv) {
            residual = std::clamp(residual * (1.0f - 0.65f * stableStrengthNeutrality), -0.28f, 0.22f);
        }
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.020f * stableStrengthNeutrality, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.040f * stableStrengthNeutrality, -0.28f, 0.22f);
    }
    if (scenePrepToneOverlapGuard > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - 0.035f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - 0.020f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[2] = std::clamp(intent.pointResidualEv[2] + 0.010f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.040f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.070f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
    }

    switch (m_AutoCalibrateVariant) {
        case ToneCurveAutoVariant::OpenShadows: {
            const float shadowBoost = 1.0f - stats.noiseRisk * 0.55f;
            intent.localBaselineEnabled = true;
            intent.localBaseline.strength = std::clamp(intent.localBaseline.strength + 0.06f * shadowBoost, 0.0f, 1.6f);
            intent.localBaseline.shadowOpening = std::clamp(intent.localBaseline.shadowOpening + 0.20f * shadowBoost, 0.0f, 2.2f);
            intent.foundationRegionEv[0] = std::clamp(intent.foundationRegionEv[0] + 0.18f * shadowBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[1] = std::clamp(intent.foundationRegionEv[1] + 0.12f * shadowBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[2] = std::clamp(intent.foundationRegionEv[2] + 0.04f * shadowBoost, -5.0f, 5.0f);
            intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + 0.02f * shadowBoost, -0.28f, 0.22f);
            intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + 0.02f * shadowBoost, -0.28f, 0.22f);
            break;
        }
        case ToneCurveAutoVariant::ProtectHighlights: {
            const float highlightProtect = 0.70f + stats.highlightPressure * 0.30f;
            intent.middleGrey = std::clamp(intent.middleGrey * std::exp2(-0.10f * highlightProtect), 0.02f, 0.50f);
            intent.logMaxEv = std::clamp(intent.logMaxEv + 0.25f * highlightProtect, 2.0f, 10.0f);
            intent.localBaselineEnabled = true;
            intent.localBaseline.highlightCompression = std::clamp(intent.localBaseline.highlightCompression + 0.16f * highlightProtect, 0.0f, 2.2f);
            intent.foundationRegionEv[3] = std::clamp(intent.foundationRegionEv[3] - 0.08f * highlightProtect, -5.0f, 5.0f);
            intent.foundationRegionEv[4] = std::clamp(intent.foundationRegionEv[4] - 0.18f * highlightProtect, -5.0f, 5.0f);
            intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.03f * highlightProtect, -0.28f, 0.22f);
            intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.05f * highlightProtect, -0.28f, 0.22f);
            break;
        }
        case ToneCurveAutoVariant::MoreContrast: {
            const float contrastBoost = 0.75f + (1.0f - flatness) * 0.25f;
            intent.localBaseline.strength = std::clamp(intent.localBaseline.strength * (1.0f - 0.10f * contrastBoost), 0.0f, 1.6f);
            intent.foundationBandWidth = std::clamp(intent.foundationBandWidth * (1.0f - 0.06f * contrastBoost), 0.5f, 8.0f);
            intent.foundationRegionEv[0] = std::clamp(intent.foundationRegionEv[0] - 0.12f * contrastBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[1] = std::clamp(intent.foundationRegionEv[1] - 0.08f * contrastBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[3] = std::clamp(intent.foundationRegionEv[3] + 0.04f * contrastBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[4] = std::clamp(intent.foundationRegionEv[4] + 0.08f * contrastBoost, -5.0f, 5.0f);
            for (float& residual : intent.pointResidualEv) {
                residual = std::clamp(residual * (1.0f + 0.16f * contrastBoost), -0.28f, 0.22f);
            }
            break;
        }
        case ToneCurveAutoVariant::Recommended:
        default:
            break;
    }

    return intent;
}

ToneCurveLayer::AutoAuthoredState ToneCurveLayer::BuildAutoAuthoredStateFromIntent(const AutoToneIntent& intent) const {
    AutoAuthoredState state;
    state.localBaselineEnabled = intent.localBaselineEnabled;
    state.localBaseline = intent.localBaseline;
    state.middleGrey = intent.middleGrey;
    state.logMinEv = intent.logMinEv;
    state.logMaxEv = intent.logMaxEv;
    state.targetAffectWidth = intent.targetAffectWidth;
    state.targetShadowProtection = intent.targetShadowProtection;
    state.targetHighlightProtection = intent.targetHighlightProtection;
    state.foundationAdaptiveAssist = intent.foundationAdaptiveAssist;
    state.foundationAssistStrength = intent.foundationAssistStrength;
    state.foundationBandWidth = intent.foundationBandWidth;
    state.foundationPreserveHue = intent.foundationPreserveHue;
    state.foundationRegionEv = intent.foundationRegionEv;

    auto sceneToCurveCoord = [&](float sceneValue) {
        if (m_Domain == ToneCurveDomain::LogScene) {
            const float ev = std::log2(std::max(sceneValue, 0.000001f) / std::max(state.middleGrey, 0.000001f));
            return Clamp01((ev - state.logMinEv) / std::max(0.0001f, state.logMaxEv - state.logMinEv));
        }
        return Clamp01(sceneValue);
    };

    auto applyLocalBaseline = [&](float sceneValue) {
        if (!state.localBaselineEnabled) {
            return std::max(sceneValue, 0.000001f);
        }
        const float safeSceneValue = std::max(sceneValue, 0.000001f);
        const float baseEv = std::log2(safeSceneValue / std::max(state.middleGrey, 0.000001f));
        const float shadowWeight = 1.0f - std::clamp((baseEv + 0.12f) / 2.48f, 0.0f, 1.0f);
        const float highlightWeight = std::clamp((baseEv - 0.12f) / 2.68f, 0.0f, 1.0f);
        const float shadowGain = std::max(0.0f, -baseEv) * shadowWeight * std::max(0.0f, state.localBaseline.shadowOpening);
        const float highlightGain = std::max(0.0f, baseEv) * highlightWeight * std::max(0.0f, state.localBaseline.highlightCompression);
        float gainEv = (shadowGain - highlightGain) * std::max(0.0f, state.localBaseline.strength);
        gainEv = std::clamp(gainEv, -4.5f, 4.0f);
        const float nearBlack = 1.0f - std::clamp((safeSceneValue - 0.003f) / 0.047f, 0.0f, 1.0f);
        const float nearClip = std::clamp((safeSceneValue - 0.95f) / 2.55f, 0.0f, 1.0f);
        if (gainEv > 0.0f) {
            gainEv *= 1.0f - 0.30f * state.localBaseline.rangeProtection * nearBlack;
        } else if (gainEv < 0.0f) {
            gainEv *= 1.0f - 0.32f * state.localBaseline.rangeProtection * nearClip;
        }
        return safeSceneValue * std::exp2(gainEv);
    };

    constexpr float kInternalMinSpacing = 0.045f;
    constexpr float kInternalMinYStep = 0.010f;
    const std::array<float, 5> evOffsets {
        -2.0f * state.foundationBandWidth,
        -1.0f * state.foundationBandWidth,
         0.0f,
         1.0f * state.foundationBandWidth,
         2.0f * state.foundationBandWidth
    };

    state.points.reserve(7);
    state.points.push_back({ 0.0f, 0.0f, ToneCurveSegmentShape::Linear });
    float previousX = 0.0f;
    float previousY = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const float sceneInput = std::max(state.middleGrey * std::exp2(evOffsets[static_cast<std::size_t>(i)]), 0.000001f);
        const float localScene = applyLocalBaseline(sceneInput);
        const float foundationScene = ApplyFoundationToSceneValue(
            localScene,
            state.middleGrey,
            state.foundationBandWidth,
            state.foundationRegionEv);
        const float baseCoord = sceneToCurveCoord(foundationScene);
        const float residualScene = std::max(
            foundationScene * std::exp2(intent.pointResidualEv[static_cast<std::size_t>(i)]),
            0.000001f);
        float x = sceneToCurveCoord(sceneInput);
        float y = sceneToCurveCoord(residualScene);
        y = std::clamp(y, baseCoord - 0.085f, baseCoord + 0.085f);
        const float minX = kInternalMinSpacing * static_cast<float>(i + 1);
        const float maxX = 1.0f - kInternalMinSpacing * static_cast<float>(5 - i);
        x = std::clamp(std::max(x, previousX + kInternalMinSpacing), minX, maxX);
        const float minY = previousY + kInternalMinYStep;
        const float maxY = 1.0f - kInternalMinYStep * static_cast<float>(5 - i);
        y = std::clamp(std::max(y, minY), minY, maxY);
        state.points.push_back({ x, y, ToneCurveSegmentShape::Linear });
        previousX = x;
        previousY = y;
    }
    state.points.push_back({ 1.0f, 1.0f, ToneCurveSegmentShape::Linear });
    return state;
}

ToneCurveLayer::AutoAuthoredState ToneCurveLayer::CaptureCurrentAutoAuthoredState() const {
    AutoAuthoredState state;
    state.localBaselineEnabled = m_LocalBaselineEnabled;
    state.localBaseline.strength = m_LocalBaselineStrength;
    state.localBaseline.shadowOpening = m_LocalShadowOpening;
    state.localBaseline.highlightCompression = m_LocalHighlightCompression;
    state.localBaseline.radius = m_LocalBaselineRadius;
    state.localBaseline.edgeProtection = m_LocalEdgeProtection;
    state.localBaseline.rangeProtection = m_LocalRangeProtection;
    state.middleGrey = m_MiddleGrey;
    state.logMinEv = m_LogMinEv;
    state.logMaxEv = m_LogMaxEv;
    state.targetAffectWidth = m_TargetAffectWidth;
    state.targetShadowProtection = m_TargetShadowProtection;
    state.targetHighlightProtection = m_TargetHighlightProtection;
    state.foundationAdaptiveAssist = m_FoundationAdaptiveAssist;
    state.foundationAssistStrength = m_FoundationAssistStrength;
    state.foundationBandWidth = m_FoundationBandWidth;
    state.foundationPreserveHue = m_FoundationPreserveHue;
    state.foundationRegionEv = GetFoundationRegionValues();
    state.points = m_PreparedPoints;
    return state;
}

ToneCurveLayer::AutoAuthoredState ToneCurveLayer::ApplyUserAdjustmentsToAutoAuthoredState(const AutoAuthoredState& state) const {
    if (!m_LastAutoAuthoredStateValid) {
        return state;
    }

    const AutoAuthoredState current = CaptureCurrentAutoAuthoredState();
    const AutoAuthoredState& authoredBase = m_LastAutoAuthoredState;
    AutoAuthoredState adjusted = state;

    auto applyDelta = [](float authoredValue, float currentValue, float previousAuthoredValue) {
        return authoredValue + (currentValue - previousAuthoredValue);
    };

    adjusted.localBaselineEnabled =
        current.localBaselineEnabled != authoredBase.localBaselineEnabled
            ? current.localBaselineEnabled
            : state.localBaselineEnabled;
    adjusted.localBaseline.strength = applyDelta(state.localBaseline.strength, current.localBaseline.strength, authoredBase.localBaseline.strength);
    adjusted.localBaseline.shadowOpening = applyDelta(state.localBaseline.shadowOpening, current.localBaseline.shadowOpening, authoredBase.localBaseline.shadowOpening);
    adjusted.localBaseline.highlightCompression = applyDelta(state.localBaseline.highlightCompression, current.localBaseline.highlightCompression, authoredBase.localBaseline.highlightCompression);
    adjusted.localBaseline.radius = applyDelta(state.localBaseline.radius, current.localBaseline.radius, authoredBase.localBaseline.radius);
    adjusted.localBaseline.edgeProtection = applyDelta(state.localBaseline.edgeProtection, current.localBaseline.edgeProtection, authoredBase.localBaseline.edgeProtection);
    adjusted.localBaseline.rangeProtection = applyDelta(state.localBaseline.rangeProtection, current.localBaseline.rangeProtection, authoredBase.localBaseline.rangeProtection);
    adjusted.middleGrey = applyDelta(state.middleGrey, current.middleGrey, authoredBase.middleGrey);
    adjusted.logMinEv = applyDelta(state.logMinEv, current.logMinEv, authoredBase.logMinEv);
    adjusted.logMaxEv = applyDelta(state.logMaxEv, current.logMaxEv, authoredBase.logMaxEv);
    adjusted.targetAffectWidth = applyDelta(state.targetAffectWidth, current.targetAffectWidth, authoredBase.targetAffectWidth);
    adjusted.targetShadowProtection = applyDelta(state.targetShadowProtection, current.targetShadowProtection, authoredBase.targetShadowProtection);
    adjusted.targetHighlightProtection = applyDelta(state.targetHighlightProtection, current.targetHighlightProtection, authoredBase.targetHighlightProtection);
    adjusted.foundationAdaptiveAssist =
        current.foundationAdaptiveAssist != authoredBase.foundationAdaptiveAssist
            ? current.foundationAdaptiveAssist
            : state.foundationAdaptiveAssist;
    adjusted.foundationAssistStrength = applyDelta(state.foundationAssistStrength, current.foundationAssistStrength, authoredBase.foundationAssistStrength);
    adjusted.foundationBandWidth = applyDelta(state.foundationBandWidth, current.foundationBandWidth, authoredBase.foundationBandWidth);
    adjusted.foundationPreserveHue =
        current.foundationPreserveHue != authoredBase.foundationPreserveHue
            ? current.foundationPreserveHue
            : state.foundationPreserveHue;
    for (std::size_t i = 0; i < adjusted.foundationRegionEv.size(); ++i) {
        adjusted.foundationRegionEv[i] = applyDelta(
            state.foundationRegionEv[i],
            current.foundationRegionEv[i],
            authoredBase.foundationRegionEv[i]);
    }
    return adjusted;
}

void ToneCurveLayer::ApplyAuthoredStateForRender(const AutoAuthoredState& state) {
    const AutoAuthoredState effectiveState = ApplyUserAdjustmentsToAutoAuthoredState(state);
    m_LocalBaselineEnabled = effectiveState.localBaselineEnabled;
    m_LocalBaselineStrength = effectiveState.localBaseline.strength;
    m_LocalShadowOpening = effectiveState.localBaseline.shadowOpening;
    m_LocalHighlightCompression = effectiveState.localBaseline.highlightCompression;
    m_LocalBaselineRadius = effectiveState.localBaseline.radius;
    m_LocalEdgeProtection = effectiveState.localBaseline.edgeProtection;
    m_LocalRangeProtection = effectiveState.localBaseline.rangeProtection;
    m_MiddleGrey = effectiveState.middleGrey;
    m_LogMinEv = effectiveState.logMinEv;
    m_LogMaxEv = effectiveState.logMaxEv;
    m_TargetAffectWidth = effectiveState.targetAffectWidth;
    m_TargetShadowProtection = effectiveState.targetShadowProtection;
    m_TargetHighlightProtection = effectiveState.targetHighlightProtection;
    m_FoundationAdaptiveAssist = effectiveState.foundationAdaptiveAssist;
    m_FoundationAssistStrength = effectiveState.foundationAssistStrength;
    m_FoundationBandWidth = effectiveState.foundationBandWidth;
    m_FoundationPreserveHue = effectiveState.foundationPreserveHue;
    m_FoundationShadows = effectiveState.foundationRegionEv[0];
    m_FoundationDarks = effectiveState.foundationRegionEv[1];
    m_FoundationMidtones = effectiveState.foundationRegionEv[2];
    m_FoundationLights = effectiveState.foundationRegionEv[3];
    m_FoundationHighlights = effectiveState.foundationRegionEv[4];
    m_PreparedPoints = state.points;
    m_ActiveGraphView = ToneCurveGraphView::Finish;
    m_LastAutoAuthoredState = state;
    m_LastAutoAuthoredStateValid = true;
    SanitizePoints();
    m_LutDirty = true;
}

void ToneCurveLayer::CapturePendingAutoRewriteFeedback() {
    if (!m_AutoSceneStatsValid ||
        m_AutoRewriteNodeId <= 0 ||
        m_AutoRewriteRequestRevision == 0) {
        return;
    }

    m_PendingAutoRewriteFeedback.valid = true;
    m_PendingAutoRewriteFeedback.nodeId = m_AutoRewriteNodeId;
    m_PendingAutoRewriteFeedback.requestRevision = m_AutoRewriteRequestRevision;
    m_PendingAutoRewriteFeedback.authoredLayerJson = Serialize();
    m_PendingAutoRewriteFeedback.authoredStateHash = HashToneCurveJson(m_PendingAutoRewriteFeedback.authoredLayerJson);
    m_PendingAutoRewriteFeedback.statsValid = m_AutoSceneStatsValid;
    m_PendingAutoRewriteFeedback.shadowPercentile = m_AutoSceneShadowPercentile;
    m_PendingAutoRewriteFeedback.midtonePercentile = m_AutoSceneMidtonePercentile;
    m_PendingAutoRewriteFeedback.highlightPercentile = m_AutoSceneHighlightPercentile;
    m_PendingAutoRewriteFeedback.clippingRatio = m_AutoSceneClippingRatio;
    m_PendingAutoRewriteFeedback.noiseRisk = m_AutoSceneNoiseRisk;
    m_PendingAutoRewriteFeedback.highlightPressure = m_AutoSceneHighlightPressure;
    m_PendingAutoRewriteFeedback.textureConfidence = m_AutoSceneTextureConfidence;
    m_PendingAutoRewriteFeedback.hdrSpreadEv = m_AutoSceneHdrSpreadEv;
    m_PendingAutoRewriteFeedback.sceneProfile = static_cast<int>(m_AutoSceneProfile);
    m_PendingAutoRewriteFeedback.recommendedBaseEv = m_AutoRecommendedBaseEv;
    m_PendingAutoRewriteFeedback.recommendedLocalStrength = m_AutoRecommendedLocalStrength;
    m_PendingAutoRewriteFeedback.recommendedShadowOpening = m_AutoRecommendedShadowOpening;
    m_PendingAutoRewriteFeedback.recommendedHighlightCompression = m_AutoRecommendedHighlightCompression;
    for (int i = 0; i < 5; ++i) {
        m_PendingAutoRewriteFeedback.recommendedFoundationEv[i] = m_AutoRecommendedFoundationEv[static_cast<std::size_t>(i)];
    }
}

void ToneCurveLayer::ClearPendingAutoRewriteFeedback() {
    m_PendingAutoRewriteFeedback = {};
}