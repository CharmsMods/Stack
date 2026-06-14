#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "App/AppShell.h"
#include "Editor/EditorModule.h"
#include "Editor/EditorRenderWorker.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Raw/RawLoader.h"
#include "Raw/RawGpuPipeline.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/MaskRenderTypes.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>

namespace {

void SetWorkingDirectoryToExecutableFolder() {
#ifdef _WIN32
    char modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path executablePath(modulePath);
    if (executablePath.has_parent_path()) {
        std::filesystem::current_path(executablePath.parent_path(), ec);
    }
#endif
}

std::size_t HashJsonValue(const nlohmann::json& value) {
    return std::hash<std::string>{}(value.dump());
}

std::size_t HashPixels(const std::vector<unsigned char>& pixels) {
    std::size_t hash = 1469598103934665603ull;
    for (unsigned char value : pixels) {
        hash ^= static_cast<std::size_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

struct ScopedFramebufferState {
    GLint framebuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;

    ScopedFramebufferState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    }

    void Restore() const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glReadBuffer(static_cast<GLenum>(readBuffer));
        glDrawBuffer(static_cast<GLenum>(drawBuffer));
    }
};

std::size_t CountPixelsWithNonZeroAlpha(const std::vector<unsigned char>& pixels) {
    std::size_t count = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            ++count;
        }
    }
    return count;
}

std::size_t CountPixelsWithNonZeroRgb(const std::vector<unsigned char>& pixels) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0) {
            ++count;
        }
    }
    return count;
}

float ComputeAverageNormalizedLuma(const std::vector<unsigned char>& pixels) {
    if (pixels.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
        const float r = static_cast<float>(pixels[i + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[i + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[i + 2]) / 255.0f;
        sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        ++count;
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

struct ValidationColorStats {
    float avgR = 0.0f;
    float avgG = 0.0f;
    float avgB = 0.0f;
    float avgLuma = 0.0f;
    float avgPixelChroma = 0.0f;
    float channelSpread = 0.0f;
    float channelRatio = 1.0f;
    float warmCoolBias = 0.0f;
    float magentaGreenBias = 0.0f;
    float biasRisk = 0.0f;
};

struct ValidationFineNoiseStats {
    float lumaHighFrequency = 0.0f;
    float chromaHighFrequency = 0.0f;
    float combined = 0.0f;
};

ValidationColorStats ComputeValidationColorStats(const std::vector<unsigned char>& pixels) {
    ValidationColorStats stats;
    if (pixels.empty()) {
        return stats;
    }

    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    double chromaSum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
        const float r = static_cast<float>(pixels[i + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[i + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[i + 2]) / 255.0f;
        const float pixelMax = (std::max)({ r, g, b });
        const float pixelMin = (std::min)({ r, g, b });
        sumR += r;
        sumG += g;
        sumB += b;
        chromaSum += pixelMax - pixelMin;
        ++count;
    }

    if (count == 0) {
        return stats;
    }

    const float invCount = 1.0f / static_cast<float>(count);
    stats.avgR = static_cast<float>(sumR) * invCount;
    stats.avgG = static_cast<float>(sumG) * invCount;
    stats.avgB = static_cast<float>(sumB) * invCount;
    stats.avgLuma = 0.2126f * stats.avgR + 0.7152f * stats.avgG + 0.0722f * stats.avgB;
    stats.avgPixelChroma = static_cast<float>(chromaSum) * invCount;
    const float maxAvg = (std::max)({ stats.avgR, stats.avgG, stats.avgB });
    const float minAvg = (std::min)({ stats.avgR, stats.avgG, stats.avgB });
    stats.channelSpread = maxAvg - minAvg;
    stats.channelRatio = maxAvg / (std::max)(0.0001f, minAvg);
    const float safeLuma = (std::max)(0.0001f, stats.avgLuma);
    stats.warmCoolBias = (stats.avgR - stats.avgB) / safeLuma;
    stats.magentaGreenBias = (((stats.avgR + stats.avgB) * 0.5f) - stats.avgG) / safeLuma;
    stats.biasRisk = std::clamp(
        (stats.channelRatio - 1.35f) / 1.15f +
            (std::abs(stats.warmCoolBias) + std::abs(stats.magentaGreenBias)) * 0.20f,
        0.0f,
        1.0f);
    return stats;
}

ValidationFineNoiseStats ComputeValidationFineNoiseStats(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    ValidationFineNoiseStats stats;
    if (pixels.empty() || width < 2 || height < 2) {
        return stats;
    }

    auto sample = [&](int x, int y, int channel) {
        const std::size_t index = static_cast<std::size_t>((y * width + x) * 4 + channel);
        return static_cast<float>(pixels[index]) / 255.0f;
    };
    auto lumaAt = [&](int x, int y) {
        return 0.2126f * sample(x, y, 0) + 0.7152f * sample(x, y, 1) + 0.0722f * sample(x, y, 2);
    };
    auto chromaAt = [&](int x, int y) {
        const float r = sample(x, y, 0);
        const float g = sample(x, y, 1);
        const float b = sample(x, y, 2);
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float cr = r - luma;
        const float cg = g - luma;
        const float cb = b - luma;
        return std::sqrt(cr * cr + cg * cg + cb * cb);
    };

    double lumaDiff = 0.0;
    double chromaDiff = 0.0;
    std::size_t count = 0;
    for (int y = 0; y + 1 < height; ++y) {
        for (int x = 0; x + 1 < width; ++x) {
            const float centerLuma = lumaAt(x, y);
            const float centerChroma = chromaAt(x, y);
            const float rightLuma = lumaAt(x + 1, y);
            const float downLuma = lumaAt(x, y + 1);
            const float rightChroma = chromaAt(x + 1, y);
            const float downChroma = chromaAt(x, y + 1);
            lumaDiff += (std::abs(centerLuma - rightLuma) + std::abs(centerLuma - downLuma)) * 0.5;
            chromaDiff += (std::abs(centerChroma - rightChroma) + std::abs(centerChroma - downChroma)) * 0.5;
            ++count;
        }
    }

    if (count > 0) {
        const float invCount = 1.0f / static_cast<float>(count);
        stats.lumaHighFrequency = static_cast<float>(lumaDiff) * invCount;
        stats.chromaHighFrequency = static_cast<float>(chromaDiff) * invCount;
        stats.combined = stats.lumaHighFrequency + stats.chromaHighFrequency * 1.4f;
    }
    return stats;
}

std::array<float, 3> ComputeValidationResolvedWhiteBalance(
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings) {
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual) {
        return settings.manualWhiteBalance;
    }
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Neutral) {
        return { 1.0f, 1.0f, 1.0f };
    }

    std::array<float, 3> wb {
        (std::max)(0.001f, metadata.cameraWhiteBalance[0]),
        (std::max)(0.001f, metadata.cameraWhiteBalance[1]),
        (std::max)(0.001f, metadata.cameraWhiteBalance[2])
    };
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Auto) {
        wb = {
            (std::max)(0.001f, metadata.daylightWhiteBalance[0]),
            (std::max)(0.001f, metadata.daylightWhiteBalance[1]),
            (std::max)(0.001f, metadata.daylightWhiteBalance[2])
        };
    }

    const float green = (std::max)(0.001f, wb[1]);
    return { wb[0] / green, 1.0f, wb[2] / green };
}

float ComputeValidationDngAutoBlend(const Raw::RawMetadata& metadata) {
    if (!metadata.hasDngAsShotNeutral ||
        !metadata.hasDngForwardMatrix1 ||
        !metadata.hasDngForwardMatrix2) {
        return -1.0f;
    }

    const float blueNeutral = metadata.dngAsShotNeutral[2];
    if (metadata.dngIlluminant1 == 17 && metadata.dngIlluminant2 != 17) {
        return 1.0f - std::clamp((blueNeutral - 0.35f) / 0.55f, 0.0f, 1.0f);
    }
    return std::clamp((0.85f - blueNeutral) / 0.50f, 0.0f, 1.0f);
}

float ReadTextureMaxRgb(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return 0.0f;
    }

    std::vector<float> pixels(static_cast<std::size_t>(width * height * 4), 0.0f);
    const ScopedFramebufferState savedState;

    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const bool readOk = glGetError() == GL_NO_ERROR;

    savedState.Restore();
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
    }

    if (!readOk) {
        return 0.0f;
    }

    float maxRgb = 0.0f;
    for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
        const float r = std::isfinite(pixels[i + 0]) ? (std::max)(0.0f, pixels[i + 0]) : 0.0f;
        const float g = std::isfinite(pixels[i + 1]) ? (std::max)(0.0f, pixels[i + 1]) : 0.0f;
        const float b = std::isfinite(pixels[i + 2]) ? (std::max)(0.0f, pixels[i + 2]) : 0.0f;
        maxRgb = (std::max)(maxRgb, (std::max)(r, (std::max)(g, b)));
    }
    return maxRgb;
}

std::vector<float> ReadTextureRgbaFloat(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return {};
    }

    std::vector<float> pixels(static_cast<std::size_t>(width * height * 4), 0.0f);
    const ScopedFramebufferState savedState;

    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const bool readOk = glGetError() == GL_NO_ERROR;

    savedState.Restore();
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
    }

    return readOk ? pixels : std::vector<float>{};
}

std::vector<unsigned char> BuildToneCurveValidationImage(int width, int height, bool alternateProfile) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / (std::max)(1, width - 1);
            const float v = static_cast<float>(y) / (std::max)(1, height - 1);
            const float radial = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f));

            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            if (!alternateProfile) {
                const float shadowBase = std::pow((std::max)(0.0f, 1.0f - u), 3.2f) * 0.08f;
                const float midtoneBand = std::exp(-10.0f * (u - 0.45f) * (u - 0.45f)) * 0.32f;
                const float highlightGlow = std::pow((std::max)(0.0f, u - 0.55f), 2.0f) * 4.0f;
                const float specular = radial < 0.14f ? (1.2f - radial * 6.0f) : 0.0f;
                r = shadowBase + midtoneBand + highlightGlow + specular * 1.2f;
                g = shadowBase * 0.85f + midtoneBand * 1.05f + highlightGlow * 0.92f + specular;
                b = shadowBase * 0.70f + midtoneBand * 0.95f + highlightGlow * 0.78f + specular * 0.75f;
            } else {
                const float lowLight = std::pow((std::max)(0.0f, 1.0f - v), 2.5f) * 0.018f;
                const float texturedLift = (std::sin(u * 36.0f) * std::cos(v * 24.0f) * 0.5f + 0.5f) * 0.022f;
                const float warmWindow = (u > 0.72f && v < 0.36f) ? 0.34f : 0.0f;
                r = lowLight + texturedLift + warmWindow * 1.08f;
                g = lowLight * 0.92f + texturedLift * 0.88f + warmWindow * 0.94f;
                b = lowLight * 1.04f + texturedLift * 0.72f + warmWindow * 0.58f;
            }

            const std::size_t idx = static_cast<std::size_t>((y * width + x) * 4);
            pixels[idx + 0] = static_cast<unsigned char>(std::clamp(r, 0.0f, 4.0f) / 4.0f * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(std::clamp(g, 0.0f, 4.0f) / 4.0f * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(std::clamp(b, 0.0f, 4.0f) / 4.0f * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

std::vector<unsigned char> BuildToneCurveBalancedValidationImage(int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / (std::max)(1, width - 1);
            const float v = static_cast<float>(y) / (std::max)(1, height - 1);
            const float gentleGradient = 0.22f + u * 0.16f + (1.0f - v) * 0.08f;
            const float skyBand = std::exp(-16.0f * (v - 0.22f) * (v - 0.22f)) * 0.12f;
            const float subjectBand = std::exp(-18.0f * (u - 0.52f) * (u - 0.52f)) * 0.10f;
            const float texture = (std::sin(u * 18.0f) * std::cos(v * 12.0f) * 0.5f + 0.5f) * 0.035f;
            const float highlightCap = (u > 0.70f && v < 0.30f) ? 0.10f : 0.0f;
            const float base = gentleGradient + skyBand + subjectBand + texture + highlightCap;

            const float r = std::clamp(base * 1.02f, 0.0f, 0.92f);
            const float g = std::clamp(base * 1.00f, 0.0f, 0.90f);
            const float b = std::clamp(base * 0.96f + skyBand * 0.06f, 0.0f, 0.88f);

            const std::size_t idx = static_cast<std::size_t>((y * width + x) * 4);
            pixels[idx + 0] = static_cast<unsigned char>(r * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(g * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(b * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

std::vector<nlohmann::json> BuildCustomToneCurvePoints(std::initializer_list<std::pair<float, float>> coords) {
    std::vector<nlohmann::json> points;
    points.reserve(coords.size());
    for (const auto& coord : coords) {
        points.push_back({
            { "x", coord.first },
            { "y", coord.second },
            { "shape", 1 }
        });
    }
    return points;
}

float RenderPassthroughMaxRgb(RenderPipeline& pipeline, unsigned int inputTexture, int width, int height) {
    if (inputTexture == 0 || width <= 0 || height <= 0) {
        return 0.0f;
    }

    static const char* kPassthroughVert = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoord;
        out vec2 vUV;
        void main() {
            vUV = aTexCoord;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* kPassthroughFrag = R"(
        #version 330 core
        in vec2 vUV;
        layout (location = 0) out vec4 FragColor;
        uniform sampler2D uInputTex;
        void main() {
            FragColor = texture(uInputTex, vUV);
        }
    )";

    const unsigned int program = GLHelpers::CreateShaderProgram(kPassthroughVert, kPassthroughFrag);
    const unsigned int targetTexture = GLHelpers::CreateEmptyTexture(width, height);
    const unsigned int fbo = GLHelpers::CreateFBO(targetTexture);
    if (program == 0 || targetTexture == 0 || fbo == 0) {
        if (fbo != 0) {
            glDeleteFramebuffers(1, &fbo);
        }
        if (targetTexture != 0) {
            glDeleteTextures(1, &targetTexture);
        }
        if (program != 0) {
            glDeleteProgram(program);
        }
        return 0.0f;
    }

    GLint prevFbo = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(program, "uInputTex"), 0);
    pipeline.GetQuad().Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    const float maxRgb = ReadTextureMaxRgb(targetTexture, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &targetTexture);
    glDeleteProgram(program);
    return maxRgb;
}

bool ValidateDevelopAutoSolveBehavior() {
    const bool defaultIntentIsNatural =
        EditorNodeGraph::DevelopAutoGuidance{}.intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;

    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    payload.integratedToneLayerJson = ToneCurveLayer().Serialize();
    payload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    payload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.018f;
    payload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.072f;
    payload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.91f;
    payload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.68f;
    payload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.57f;
    payload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.71f;
    payload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 4.6f;
    payload.integratedToneLayerJson["autoRecommendedBaseEv"] = 1.35f;
    payload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.14f;
    payload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.28f;
    payload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.23f;

    payload.autoGuidance.autoStrength = 1.35f;
    payload.autoGuidance.exposureBias = 0.32f;
    payload.autoGuidance.dynamicRange = 1.85f;
    payload.autoGuidance.shadowLift = 0.70f;
    payload.autoGuidance.highlightGuard = 0.55f;
    payload.autoGuidance.highlightCharacter = -0.20f;
    payload.autoGuidance.contrastBias = 0.18f;

    const Raw::RawDevelopSettings settingsBefore = payload.settings;
    const Raw::RawDetailFusionSettings scenePrepBefore = payload.scenePrepSettings;
    const std::uint64_t requestIdBefore =
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));

    Raw::RawMetadata metadata;
    metadata.isDng = true;
    metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    metadata.whiteLevel = 16383.0f;
    metadata.blackLevel = 512.0f;
    metadata.hasDngBaselineExposure = true;
    metadata.dngBaselineExposure = -0.10f;
    metadata.hasDngForwardMatrix1 = true;
    metadata.cameraWhiteBalance = { 2.02f, 1.0f, 1.61f, 1.0f };
    metadata.daylightWhiteBalance = { 1.0f, 1.0f, 1.0f, 1.0f };

    EditorModule::ApplyDevelopAutoSolve(payload, metadata, true);

    const bool exposureAuthored = payload.settings.exposureStops > settingsBefore.exposureStops + 0.35f;
    const bool highlightAuthored =
        payload.settings.highlightMode != Raw::HighlightReconstructionMode::Off &&
        payload.settings.highlightStrength > settingsBefore.highlightStrength + 0.02f &&
        payload.settings.highlightThreshold < settingsBefore.highlightThreshold - 0.01f;
    const bool cleanupAuthored =
        payload.settings.mosaicDenoise.enabled &&
        payload.settings.mosaicDenoise.lumaStrength > settingsBefore.mosaicDenoise.lumaStrength &&
        payload.settings.falseColorSuppression > settingsBefore.falseColorSuppression;
    const bool scenePrepAuthored =
        payload.scenePrepSettings.maxEvBias > scenePrepBefore.maxEvBias + 0.20f &&
        payload.scenePrepSettings.highlightProtectionBias > scenePrepBefore.highlightProtectionBias + 0.10f &&
        payload.scenePrepSettings.strength > scenePrepBefore.strength;
    const bool finishQueued =
        payload.integratedToneLayerJson.value("autoCalibratePending", false) &&
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0)) > requestIdBefore;
    const bool requestedGuidanceForwarded =
        std::abs(payload.integratedToneLayerJson.value("autoRequestedSceneAssistStrength", -99.0f) - payload.autoGuidance.autoStrength) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedDynamicRange", -99.0f) - payload.autoGuidance.dynamicRange) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedHighlightBias", -99.0f) - payload.autoGuidance.highlightGuard) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedBrightnessIntent", -99.0f) - payload.autoGuidance.exposureBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedRawExposurePreferenceEv", -99.0f) - payload.autoGuidance.exposureBias * 2.0f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedSubjectSceneBias", -99.0f) - payload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedMoodReadabilityBias", -99.0f) - payload.autoGuidance.moodReadabilityBias) < 0.0001f;
    bool selectedCandidateGuidanceForwarded = false;
    bool selectedCandidateScoreComponentsWritten = false;
    const nlohmann::json& candidateSolves = payload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    const std::string selectedCandidateId = payload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    if (candidateSolves.is_array() && !selectedCandidateId.empty()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (!candidate.is_object() || candidate.value("id", std::string()) != selectedCandidateId) {
                continue;
            }
            const nlohmann::json& guidance = candidate.value("guidance", nlohmann::json::object());
            selectedCandidateGuidanceForwarded =
                std::abs(payload.integratedToneLayerJson.value("autoSceneAssistStrength", -99.0f) - guidance.value("autoStrength", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f) - guidance.value("brightnessIntent", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoDynamicRange", -99.0f) - guidance.value("dynamicRange", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoHighlightBias", -99.0f) - guidance.value("highlightGuard", -98.0f)) < 0.0001f &&
                std::abs(payload.integratedToneLayerJson.value("autoContrastBias", -99.0f) - guidance.value("contrastBias", -98.0f)) < 0.0001f;
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            const float uniqueness = dimensions.value("candidateUniqueness", -1.0f);
            selectedCandidateScoreComponentsWritten =
                scoreComponents.value("version", std::string()) == "ParameterScoreComponentsV1" &&
                scoreComponents.value("scoreSource", std::string()).find("parameter") != std::string::npos &&
                std::abs(scoreComponents.value("finalScore", -1.0f) - candidate.value("score", -2.0f)) < 0.0001f &&
                dimensions.contains("midtonePlacement") &&
                dimensions.contains("highlightIntegrity") &&
                dimensions.contains("shadowCleanliness") &&
                dimensions.contains("dynamicRangeFit") &&
                dimensions.contains("contrastShape") &&
                dimensions.contains("brightnessHierarchy") &&
                dimensions.contains("noiseTextureQuality") &&
                dimensions.contains("localArtifactSafety") &&
                dimensions.contains("modeIntentFit") &&
                uniqueness >= 0.0f &&
                uniqueness <= 1.0f &&
                risks.contains("highlightDamageRisk") &&
                risks.contains("shadowNoiseRisk") &&
                risks.contains("flatteningRisk") &&
                risks.contains("dataRiskPenalty");
            break;
        }
    }
    const bool candidateDiagnosticsWritten =
        payload.integratedToneLayerJson.value("autoCandidateSolveVersion", std::string()) == "ParameterCandidatesV1" &&
        payload.integratedToneLayerJson.value("autoCandidateScoreVersion", std::string()) == "ParameterScoreComponentsV1" &&
        candidateSolves.is_array() &&
        candidateSolves.size() >= 2 &&
        payload.integratedToneLayerJson.value("autoCandidateSelectionIsAuthoredState", false) &&
        payload.integratedToneLayerJson.value("autoCandidateSurvivorCount", 0) >= 1 &&
        payload.integratedToneLayerJson.value("autoCandidateSelectedScore", 0.0f) > 0.0f &&
        payload.integratedToneLayerJson.value("autoCandidateConvergencePass", 0) > 0 &&
        payload.integratedToneLayerJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0)) != 0;
    const nlohmann::json initialRenderedLoop =
        payload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedFeedbackLoopAwaitingMetrics =
        payload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoopVersion", std::string()) == "RenderedFeedbackLoopV1" &&
        initialRenderedLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        initialRenderedLoop.value("state", std::string()) == "awaitingRenderedMetrics" &&
        initialRenderedLoop.value("nextStep", std::string()) == "renderCandidates" &&
        initialRenderedLoop.value("requiresRenderedMetrics", false) &&
        !initialRenderedLoop.value("requiresAutoSolve", true) &&
        initialRenderedLoop.value("pass", -1) == 0 &&
        initialRenderedLoop.value("maxPasses", 0) == 3 &&
        initialRenderedLoop.value("solveFingerprint", static_cast<std::uint64_t>(0)) ==
            payload.integratedToneLayerJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(1));
    const nlohmann::json initialContinuationPolicy =
        payload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyAwaitingMetrics =
        payload.integratedToneLayerJson.value("autoCandidateRenderedContinuationVersion", std::string()) == "RenderedContinuationV1" &&
        initialContinuationPolicy.value("version", std::string()) == "RenderedContinuationV1" &&
        initialContinuationPolicy.value("decision", std::string()) == "waitForRenderedMetrics" &&
        initialContinuationPolicy.value("nextStep", std::string()) == "renderCandidates" &&
        initialContinuationPolicy.value("requiresRenderedMetrics", false) &&
        !initialContinuationPolicy.value("requiresAutoSolve", true) &&
        initialContinuationPolicy.value("bounded", false) &&
        initialContinuationPolicy.value("pass", -1) == 0 &&
        initialContinuationPolicy.value("maxPasses", 0) == 3;
    const nlohmann::json initialConvergenceEvidence =
        payload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceAwaitingMetrics =
        payload.integratedToneLayerJson.value("autoCandidateConvergenceEvidenceVersion", std::string()) == "ConvergenceEvidenceV1" &&
        initialConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        payload.integratedToneLayerJson.value("autoCandidateConvergenceState", std::string()) == "awaitingRenderedMetrics" &&
        payload.integratedToneLayerJson.value("autoCandidateConvergenceDecision", std::string()) == "waitForRenderedMetrics" &&
        payload.integratedToneLayerJson.value("autoCandidateConvergenceShouldContinue", false) &&
        initialConvergenceEvidence.value("shouldContinue", false) &&
        initialConvergenceEvidence.value("requiresRenderedMetrics", false) &&
        initialConvergenceEvidence.value("rendered", nlohmann::json::object()).value("metricsReadyForCurrentSolve", true) == false &&
        initialConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "waitForRenderedMetrics";
    std::uint64_t renderedMemoryCandidateFingerprint = 0;
    std::string renderedMemoryCandidateId;
    std::string renderedMemoryCandidateLabel;
    if (candidateSolves.is_array()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) != "base" &&
                candidate.value("status", std::string()) == "survivor") {
                renderedMemoryCandidateId = candidate.value("id", std::string());
                renderedMemoryCandidateLabel = candidate.value("label", renderedMemoryCandidateId);
                renderedMemoryCandidateFingerprint =
                    candidate.value("guidanceFingerprint", static_cast<std::uint64_t>(0));
                break;
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload renderedRejectionMemoryPayload = payload;
    if (renderedMemoryCandidateFingerprint != 0) {
        renderedRejectionMemoryPayload.integratedToneLayerJson["autoCandidateRenderedRejectionMemory"] =
            nlohmann::json::array({
                {
                    { "id", renderedMemoryCandidateId },
                    { "label", renderedMemoryCandidateLabel },
                    { "guidanceFingerprint", renderedMemoryCandidateFingerprint },
                    { "status", "renderedRejectedDamage" },
                    { "reason", "Synthetic rendered memory rejected this exact authored survivor state." },
                    { "renderScore", 0.18f },
                    { "solveFingerprint", renderedRejectionMemoryPayload.integratedToneLayerJson.value(
                        "autoCandidateSolveFingerprint",
                        static_cast<std::uint64_t>(0)) }
                }
            });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedRejectionMemoryPayload, metadata, true);
    bool renderedRejectionMemorySuppressesRepeat = false;
    const nlohmann::json repeatedRenderedMemoryCandidates =
        renderedRejectionMemoryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (repeatedRenderedMemoryCandidates.is_array()) {
        for (const nlohmann::json& candidate : repeatedRenderedMemoryCandidates) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != renderedMemoryCandidateId) {
                continue;
            }
            renderedRejectionMemorySuppressesRepeat =
                candidate.value("status", std::string()) == "rejectedMemory" &&
                candidate.value("renderedMemoryRejected", false) &&
                candidate.value("guidanceFingerprint", static_cast<std::uint64_t>(0)) ==
                    renderedMemoryCandidateFingerprint &&
                renderedRejectionMemoryPayload.integratedToneLayerJson.value(
                    "autoCandidateRenderedRejectedMemorySuppressionCount",
                    0) >= 1;
            break;
        }
    }
    const nlohmann::json stageSolves =
        payload.integratedToneLayerJson.value("autoStageSolveStages", nlohmann::json::array());
    const nlohmann::json stageFingerprints =
        payload.integratedToneLayerJson.value("autoStageFingerprints", nlohmann::json::object());
    auto hasStageState = [&](const std::string& state) {
        if (!stageSolves.is_array()) {
            return false;
        }
        for (const nlohmann::json& stage : stageSolves) {
            if (stage.is_object() &&
                stage.value("state", std::string()) == state &&
                !stage.value("status", std::string()).empty()) {
                return true;
            }
        }
        return false;
    };
    const bool stagedAutoSolveDiagnosticsWritten =
        payload.integratedToneLayerJson.value("autoStageSolveVersion", std::string()) == "StagedAutoSolveV1" &&
        payload.integratedToneLayerJson.value("autoStageCacheSplitStatus", std::string()).find("logicalOnly") != std::string::npos &&
        payload.integratedToneLayerJson.value("autoStageCurrentRawExposureInsideRawBase", false) &&
        !payload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string()).empty() &&
        stageFingerprints.is_object() &&
        stageFingerprints.value("metadata", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("rawBase", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("rawGlobal", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("scenePrep", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("finishTone", static_cast<std::uint64_t>(0)) != 0 &&
        stageFingerprints.value("finalValidation", static_cast<std::uint64_t>(0)) != 0 &&
        stageSolves.is_array() &&
        stageSolves.size() >= 13 &&
        hasStageState("RENDER_RAW_BASE") &&
        hasStageState("SOLVE_GLOBAL") &&
        hasStageState("RENDER_PREFINISH") &&
        hasStageState("SOLVE_FINISH_TONE") &&
        hasStageState("VALIDATE_FINAL");
    const nlohmann::json learningRecord =
        payload.integratedToneLayerJson.value("autoCandidateLearningRecord", nlohmann::json::object());
    const nlohmann::json learningEvents =
        learningRecord.value("events", nlohmann::json::array());
    bool learningHasSelectedOutcome = false;
    if (learningEvents.is_array()) {
        for (const nlohmann::json& event : learningEvents) {
            if (!event.is_object()) {
                continue;
            }
            if (event.value("type", std::string()) == "candidateSelected" &&
                event.value("candidateId", std::string()) == selectedCandidateId &&
                event.value("status", std::string()) == "selected" &&
                event.value("guidanceVector", nlohmann::json::object()).is_object()) {
                learningHasSelectedOutcome = true;
                break;
            }
        }
    }
    const nlohmann::json currentImageLearning =
        learningRecord.value("currentImageLearning", nlohmann::json::object());
    const nlohmann::json futureImageLearning =
        learningRecord.value("futureImageLearning", nlohmann::json::object());
    const nlohmann::json userChoiceLearning =
        learningRecord.value("userChoiceLearning", nlohmann::json::object());
    const bool candidateLearningRecordedNotApplied =
        payload.integratedToneLayerJson.value("autoCandidateLearningVersion", std::string()) == "CandidateOutcomeLearningV1" &&
        payload.integratedToneLayerJson.value("autoCandidateLearningStatus", std::string()) == "recordedNotApplied" &&
        payload.integratedToneLayerJson.value("autoCandidateLearningRecorded", false) &&
        !payload.integratedToneLayerJson.value("autoCandidateLearningApplied", true) &&
        !payload.integratedToneLayerJson.value("autoCandidateLearningAppliedToCurrentImage", true) &&
        !payload.integratedToneLayerJson.value("autoCandidateLearningAppliedToFutureImages", true) &&
        payload.integratedToneLayerJson.value("autoCandidateLearningEventCount", 0) >= 1 &&
        learningRecord.is_object() &&
        learningRecord.value("selectedId", std::string()) == selectedCandidateId &&
        learningRecord.value("applied", true) == false &&
        learningRecord.value("eventCount", 0) == payload.integratedToneLayerJson.value("autoCandidateLearningEventCount", -1) &&
        currentImageLearning.value("recorded", false) &&
        !currentImageLearning.value("applied", true) &&
        !futureImageLearning.value("applied", true) &&
        userChoiceLearning.value("status", std::string()) == "deferredUntilCandidateSelectionUi" &&
        learningHasSelectedOutcome;
    EditorNodeGraph::RawDevelopPayload rejectedMemorySeedPayload = payload;
    rejectedMemorySeedPayload.autoGuidance.autoStrength = 1.70f;
    rejectedMemorySeedPayload.autoGuidance.exposureBias = 0.55f;
    rejectedMemorySeedPayload.autoGuidance.dynamicRange = 2.25f;
    rejectedMemorySeedPayload.autoGuidance.shadowLift = 1.00f;
    rejectedMemorySeedPayload.autoGuidance.highlightGuard = 0.10f;
    rejectedMemorySeedPayload.autoGuidance.highlightCharacter = -0.25f;
    rejectedMemorySeedPayload.autoGuidance.contrastBias = 0.18f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.004f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.052f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.985f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.055f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.94f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.95f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.18f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 5.5f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 1.20f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.20f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.35f;
    rejectedMemorySeedPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.35f;
    EditorModule::ApplyDevelopAutoSolve(rejectedMemorySeedPayload, metadata, true);

    const nlohmann::json& candidateRejectedMemory =
        rejectedMemorySeedPayload.integratedToneLayerJson.value("autoCandidateRejectedMemory", nlohmann::json::array());
    const bool rejectedCandidateMemoryRecorded =
        candidateRejectedMemory.is_array() &&
        !candidateRejectedMemory.empty() &&
        rejectedMemorySeedPayload.integratedToneLayerJson.value("autoCandidateContextFingerprint", static_cast<std::uint64_t>(0)) != 0;
    EditorNodeGraph::RawDevelopPayload rejectedMemoryPayload = rejectedMemorySeedPayload;
    EditorModule::ApplyDevelopAutoSolve(rejectedMemoryPayload, metadata, true);
    const nlohmann::json& repeatedCandidateSolves =
        rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    bool repeatedCandidateRejectedFromMemory = false;
    if (repeatedCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : repeatedCandidateSolves) {
            if (candidate.is_object() &&
                candidate.value("status", std::string()) == "rejectedMemory") {
                repeatedCandidateRejectedFromMemory = true;
                break;
            }
        }
    }
    const bool rejectedCandidateMemorySuppressesRepeat =
        rejectedCandidateMemoryRecorded &&
        repeatedCandidateRejectedFromMemory &&
        rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateRejectedMemorySuppressionCount", 0) > 0 &&
        rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateRejectedCount", 0) >=
            payload.integratedToneLayerJson.value("autoCandidateRejectedCount", 0);
    const bool candidateSolveCanBiasAuthoredGuidance =
        std::abs(payload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f) - payload.autoGuidance.exposureBias) > 0.0001f ||
        std::abs(payload.integratedToneLayerJson.value("autoDynamicRange", -99.0f) - payload.autoGuidance.dynamicRange) > 0.0001f ||
        std::abs(payload.integratedToneLayerJson.value("autoContrastBias", -99.0f) - payload.autoGuidance.contrastBias) > 0.0001f;

    auto guidanceFromToneJson = [](const nlohmann::json& toneJson, EditorNodeGraph::DevelopAutoGuidance fallback) {
        fallback.autoStrength = toneJson.value("autoSceneAssistStrength", fallback.autoStrength);
        fallback.exposureBias = toneJson.value("autoBrightnessIntent", fallback.exposureBias);
        fallback.dynamicRange = toneJson.value("autoDynamicRange", fallback.dynamicRange);
        fallback.shadowLift = toneJson.value("autoShadowBias", fallback.shadowLift);
        fallback.highlightGuard = toneJson.value("autoHighlightBias", fallback.highlightGuard);
        fallback.highlightCharacter = toneJson.value("autoHighlightCharacter", fallback.highlightCharacter);
        fallback.contrastBias = toneJson.value("autoContrastBias", fallback.contrastBias);
        fallback.subjectSceneBias = toneJson.value("autoSubjectSceneBias", fallback.subjectSceneBias);
        fallback.moodReadabilityBias = toneJson.value("autoMoodReadabilityBias", fallback.moodReadabilityBias);
        EditorModule::NormalizeDevelopAutoGuidance(fallback);
        return fallback;
    };
    auto guidanceFromCandidateJson = [](const nlohmann::json& guidanceJson, EditorNodeGraph::DevelopAutoGuidance fallback) {
        fallback.autoStrength = guidanceJson.value("autoStrength", fallback.autoStrength);
        fallback.exposureBias = guidanceJson.value("brightnessIntent", fallback.exposureBias);
        fallback.dynamicRange = guidanceJson.value("dynamicRange", fallback.dynamicRange);
        fallback.shadowLift = guidanceJson.value("shadowLift", fallback.shadowLift);
        fallback.highlightGuard = guidanceJson.value("highlightGuard", fallback.highlightGuard);
        fallback.highlightCharacter = guidanceJson.value("highlightCharacter", fallback.highlightCharacter);
        fallback.contrastBias = guidanceJson.value("contrastBias", fallback.contrastBias);
        fallback.subjectSceneBias = guidanceJson.value("subjectSceneBias", fallback.subjectSceneBias);
        fallback.moodReadabilityBias = guidanceJson.value("moodReadabilityBias", fallback.moodReadabilityBias);
        EditorModule::NormalizeDevelopAutoGuidance(fallback);
        return fallback;
    };

    EditorNodeGraph::DevelopAutoGuidance cleanShadowCandidateGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance preserveTextureCandidateGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance highlightProtectedMidsGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance broadHighlightGuardGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance finishToneProbeGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance naturalContrastGuardGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance brightHighlightRolloffGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance luminousHighlightAnchorGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance specularHighlightToleranceGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance haloSafeLocalRangeGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance localRangeGuardGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance shadowReadabilityLiftGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance shadowNoiseFloorGuidance = payload.autoGuidance;
    EditorNodeGraph::DevelopAutoGuidance whiteBalanceProbeGuidance = payload.autoGuidance;
    bool cleanShadowCandidateGenerated = false;
    bool preserveTextureCandidateGenerated = false;
    bool highlightProtectedMidsGenerated = false;
    bool highlightProtectedMidsEligible = false;
    bool highlightProtectedMidsMeaningfullyDifferent = false;
    bool broadHighlightGuardGenerated = false;
    bool broadHighlightGuardEligible = false;
    bool broadHighlightGuardHumanReadable = false;
    bool broadHighlightGuardDiagnosticsWritten = false;
    bool finishToneProbeGenerated = false;
    bool finishToneProbeEligible = false;
    bool finishToneProbeHumanReadable = false;
    bool finishToneProbeMeaningfullyDifferent = false;
    bool naturalContrastGuardGenerated = false;
    bool naturalContrastGuardEligible = false;
    bool naturalContrastGuardHumanReadable = false;
    bool naturalContrastGuardDiagnosticsWritten = false;
    bool brightHighlightRolloffGenerated = false;
    bool brightHighlightRolloffEligible = false;
    bool brightHighlightRolloffHumanReadable = false;
    bool brightHighlightRolloffDiagnosticsWritten = false;
    bool luminousHighlightAnchorGenerated = false;
    bool luminousHighlightAnchorEligible = false;
    bool luminousHighlightAnchorHumanReadable = false;
    bool luminousHighlightAnchorDiagnosticsWritten = false;
    bool specularHighlightToleranceGenerated = false;
    bool specularHighlightToleranceEligible = false;
    bool specularHighlightToleranceHumanReadable = false;
    bool specularHighlightToleranceDiagnosticsWritten = false;
    bool regionalEvidenceDiagnosticsWritten = false;
    bool haloSafeLocalRangeGenerated = false;
    bool haloSafeLocalRangeEligible = false;
    bool haloSafeLocalRangeHumanReadable = false;
    bool haloSafeLocalRangeDiagnosticsWritten = false;
    bool localRangeGuardGenerated = false;
    bool localRangeGuardEligible = false;
    bool localRangeGuardDiagnosticsWritten = false;
    bool shadowReadabilityLiftGenerated = false;
    bool shadowReadabilityLiftEligible = false;
    bool shadowReadabilityLiftHumanReadable = false;
    bool shadowReadabilityLiftDiagnosticsWritten = false;
    bool shadowNoiseFloorGenerated = false;
    bool shadowNoiseFloorEligible = false;
    bool shadowNoiseFloorDiagnosticsWritten = false;
    std::string finishToneProbeId;
    bool modeNeighborCandidateGenerated = false;
    bool modeNeighborCandidateEligible = false;
    bool modeNeighborCandidateHumanReadable = false;
    bool modeNeighborCandidateMeaningfullyDifferent = false;
    bool whiteBalanceProbeGenerated = false;
    bool whiteBalanceProbeEligible = false;
    bool whiteBalanceProbeHumanReadable = false;
    bool whiteBalanceProbeDiagnosticsWritten = false;
    std::string whiteBalanceProbeId;
    std::string whiteBalanceProbeMode;
    auto isFinishToneProbeId = [](const std::string& id) {
        return
            id == "toneSofterRolloff" ||
            id == "naturalContrastGuard" ||
            id == "brightHighlightRolloff" ||
            id == "luminousHighlightAnchor" ||
            id == "specularHighlightTolerance" ||
            id == "tonePunchierShape" ||
            id == "toneFlatterEditing" ||
            id == "toneDarkerToe";
    };
    auto isWhiteBalanceProbeId = [](const std::string& id) {
        return
            id == "wbDaylightCorrection" ||
            id == "wbNeutralCorrection" ||
            id == "wbCameraMood";
    };
    if (candidateSolves.is_array()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            if (id.rfind("modeNeighbor", 0) == 0) {
                modeNeighborCandidateGenerated = true;
                modeNeighborCandidateEligible = modeNeighborCandidateEligible || eligible;
                const std::string label = candidate.value("label", std::string());
                const std::string reason = candidate.value("reason", std::string());
                modeNeighborCandidateHumanReadable =
                    modeNeighborCandidateHumanReadable ||
                    (!label.empty() &&
                     label.find("modeNeighbor") == std::string::npos &&
                     reason.find("neighboring") != std::string::npos);
                const nlohmann::json changes =
                    candidate.value("changes", nlohmann::json::object());
                if (changes.is_object()) {
                    const float totalDelta =
                        std::fabs(changes.value("brightnessIntentDelta", 0.0f)) +
                        std::fabs(changes.value("dynamicRangeDelta", 0.0f)) +
                        std::fabs(changes.value("shadowLiftDelta", 0.0f)) +
                        std::fabs(changes.value("highlightGuardDelta", 0.0f)) +
                        std::fabs(changes.value("contrastBiasDelta", 0.0f));
                    modeNeighborCandidateMeaningfullyDifferent =
                        modeNeighborCandidateMeaningfullyDifferent || totalDelta > 0.30f;
                }
            }
            if (isFinishToneProbeId(id)) {
                finishToneProbeGenerated = true;
                finishToneProbeEligible = finishToneProbeEligible || eligible;
                if (finishToneProbeId.empty() || eligible) {
                    finishToneProbeId = id;
                    finishToneProbeGuidance =
                        guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                }
                const std::string label = candidate.value("label", std::string());
                const std::string reason = candidate.value("reason", std::string());
                finishToneProbeHumanReadable =
                    finishToneProbeHumanReadable ||
                    (!label.empty() &&
                     label.find("tone") == std::string::npos &&
                     label.find("Tone") != std::string::npos &&
                     reason.find("finish-tone") != std::string::npos);
                const nlohmann::json changes =
                    candidate.value("changes", nlohmann::json::object());
                if (changes.is_object()) {
                    const float toneDelta =
                        std::fabs(changes.value("highlightCharacterDelta", 0.0f)) +
                        std::fabs(changes.value("contrastBiasDelta", 0.0f));
                    const float upstreamDelta =
                        std::fabs(changes.value("brightnessIntentDelta", 0.0f)) +
                        std::fabs(changes.value("shadowLiftDelta", 0.0f));
                    finishToneProbeMeaningfullyDifferent =
                        finishToneProbeMeaningfullyDifferent ||
                        (toneDelta > 0.26f && upstreamDelta < 0.24f);
                }
                if (id == "brightHighlightRolloff") {
                    brightHighlightRolloffGenerated = true;
                    brightHighlightRolloffEligible = brightHighlightRolloffEligible || eligible;
                    brightHighlightRolloffGuidance =
                        guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                    const nlohmann::json scoreComponents =
                        candidate.value("scoreComponents", nlohmann::json::object());
                    const nlohmann::json dimensions =
                        scoreComponents.value("dimensions", nlohmann::json::object());
                    const nlohmann::json changes =
                        candidate.value("changes", nlohmann::json::object());
                    brightHighlightRolloffHumanReadable =
                        brightHighlightRolloffHumanReadable ||
                        (label.find("Bright") != std::string::npos &&
                         label.find("Highlight") != std::string::npos &&
                         reason.find("bright light") != std::string::npos &&
                         reason.find("does not recover") != std::string::npos);
                    brightHighlightRolloffDiagnosticsWritten =
                        brightHighlightRolloffDiagnosticsWritten ||
                        (dimensions.value("brightnessHierarchy", -1.0f) > 0.50f &&
                         changes.value("highlightCharacterDelta", 0.0f) > 0.18f &&
                         std::fabs(changes.value("brightnessIntentDelta", 0.0f)) < 0.08f);
                }
                if (id == "luminousHighlightAnchor") {
                    luminousHighlightAnchorGenerated = true;
                    luminousHighlightAnchorEligible = luminousHighlightAnchorEligible || eligible;
                    luminousHighlightAnchorGuidance =
                        guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                    const nlohmann::json scoreComponents =
                        candidate.value("scoreComponents", nlohmann::json::object());
                    const nlohmann::json dimensions =
                        scoreComponents.value("dimensions", nlohmann::json::object());
                    const nlohmann::json signals =
                        scoreComponents.value("signals", nlohmann::json::object());
                    const nlohmann::json changes =
                        candidate.value("changes", nlohmann::json::object());
                    luminousHighlightAnchorHumanReadable =
                        luminousHighlightAnchorHumanReadable ||
                        (label.find("Luminous") != std::string::npos &&
                         label.find("Highlight") != std::string::npos &&
                         reason.find("stay luminous") != std::string::npos &&
                         reason.find("does not recover clipped detail") != std::string::npos);
                    luminousHighlightAnchorDiagnosticsWritten =
                        luminousHighlightAnchorDiagnosticsWritten ||
                        (dimensions.value("luminousHighlightAnchor", -1.0f) > 0.58f &&
                         dimensions.value("brightnessHierarchy", -1.0f) > 0.54f &&
                         dimensions.value("contrastShape", -1.0f) > 0.54f &&
                         signals.value("highlightBrightnessSignal", -1.0f) >= 0.0f &&
                         changes.value("highlightCharacterDelta", 0.0f) > 0.22f &&
                         changes.value("contrastBiasDelta", 0.0f) > 0.10f &&
                         changes.value("dynamicRangeDelta", 0.0f) < -0.04f &&
                         std::fabs(changes.value("brightnessIntentDelta", 0.0f)) < 0.04f);
                }
                if (id == "naturalContrastGuard") {
                    naturalContrastGuardGenerated = true;
                    naturalContrastGuardEligible = naturalContrastGuardEligible || eligible;
                    naturalContrastGuardGuidance =
                        guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                    const nlohmann::json scoreComponents =
                        candidate.value("scoreComponents", nlohmann::json::object());
                    const nlohmann::json dimensions =
                        scoreComponents.value("dimensions", nlohmann::json::object());
                    const nlohmann::json changes =
                        candidate.value("changes", nlohmann::json::object());
                    naturalContrastGuardHumanReadable =
                        naturalContrastGuardHumanReadable ||
                        (label.find("Natural") != std::string::npos &&
                         label.find("Contrast") != std::string::npos &&
                         reason.find("believable separation") != std::string::npos &&
                         reason.find("lighting hierarchy") != std::string::npos);
                    naturalContrastGuardDiagnosticsWritten =
                        naturalContrastGuardDiagnosticsWritten ||
                        (dimensions.value("naturalContrastGuard", -1.0f) > 0.54f &&
                         dimensions.value("brightnessHierarchy", -1.0f) > 0.54f &&
                         dimensions.value("contrastShape", -1.0f) > 0.54f &&
                         changes.value("contrastBiasDelta", 0.0f) > 0.18f &&
                         changes.value("dynamicRangeDelta", 0.0f) < -0.06f &&
                         std::fabs(changes.value("brightnessIntentDelta", 0.0f)) < 0.04f);
                }
                if (id == "specularHighlightTolerance") {
                    specularHighlightToleranceGenerated = true;
                    specularHighlightToleranceEligible = specularHighlightToleranceEligible || eligible;
                    specularHighlightToleranceGuidance =
                        guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                    const nlohmann::json scoreComponents =
                        candidate.value("scoreComponents", nlohmann::json::object());
                    const nlohmann::json dimensions =
                        scoreComponents.value("dimensions", nlohmann::json::object());
                    const nlohmann::json changes =
                        candidate.value("changes", nlohmann::json::object());
                    specularHighlightToleranceHumanReadable =
                        specularHighlightToleranceHumanReadable ||
                        (label.find("Specular") != std::string::npos &&
                         label.find("Highlight") != std::string::npos &&
                         reason.find("tiny specular") != std::string::npos &&
                         reason.find("not clipped-data recovery") != std::string::npos);
                    specularHighlightToleranceDiagnosticsWritten =
                        specularHighlightToleranceDiagnosticsWritten ||
                        (dimensions.value("specularTolerance", -1.0f) > 0.54f &&
                         dimensions.value("brightnessHierarchy", -1.0f) > 0.50f &&
                         changes.value("highlightCharacterDelta", 0.0f) > 0.24f &&
                         changes.value("highlightGuardDelta", 0.0f) < -0.10f);
                }
            }
            if (isWhiteBalanceProbeId(id)) {
                whiteBalanceProbeGenerated = true;
                whiteBalanceProbeEligible = whiteBalanceProbeEligible || eligible;
                if (whiteBalanceProbeId.empty() || eligible) {
                    whiteBalanceProbeId = id;
                    whiteBalanceProbeGuidance =
                        guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                }
                const std::string label = candidate.value("label", std::string());
                const std::string reason = candidate.value("reason", std::string());
                whiteBalanceProbeHumanReadable =
                    whiteBalanceProbeHumanReadable ||
                    (!label.empty() &&
                     (label.find("WB") != std::string::npos || label.find("White") != std::string::npos) &&
                     reason.find("white balance") != std::string::npos);
                const nlohmann::json rawOverrides =
                    candidate.value("rawOverrides", nlohmann::json::object());
                const nlohmann::json changes =
                    candidate.value("changes", nlohmann::json::object());
                const nlohmann::json scoreComponents =
                    candidate.value("scoreComponents", nlohmann::json::object());
                const nlohmann::json dimensions =
                    scoreComponents.value("dimensions", nlohmann::json::object());
                if (rawOverrides.is_object() &&
                    changes.is_object() &&
                    dimensions.is_object()) {
                    const std::string mode =
                        rawOverrides.value("whiteBalanceMode", std::string());
                    whiteBalanceProbeMode = whiteBalanceProbeMode.empty()
                        ? mode
                        : whiteBalanceProbeMode;
                    whiteBalanceProbeDiagnosticsWritten =
                        whiteBalanceProbeDiagnosticsWritten ||
                        (!mode.empty() &&
                         changes.value("whiteBalanceMode", std::string()) == mode &&
                         dimensions.value("colorPlausibility", -1.0f) >= 0.0f &&
                         dimensions.value("moodColorPreservation", -1.0f) >= 0.0f);
                }
            }
            if (id == "highlightProtectedMids") {
                highlightProtectedMidsGenerated = true;
                highlightProtectedMidsEligible = highlightProtectedMidsEligible || eligible;
                highlightProtectedMidsGuidance =
                    guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                const nlohmann::json changes =
                    candidate.value("changes", nlohmann::json::object());
                if (changes.is_object()) {
                    const bool lowersBrightness = changes.value("brightnessIntentDelta", 0.0f) < -0.10f;
                    const bool liftsLocalRange =
                        changes.value("dynamicRangeDelta", 0.0f) > 0.20f &&
                        changes.value("shadowLiftDelta", 0.0f) > 0.10f &&
                        changes.value("highlightGuardDelta", 0.0f) > 0.20f;
                    highlightProtectedMidsMeaningfullyDifferent =
                        highlightProtectedMidsMeaningfullyDifferent || (lowersBrightness && liftsLocalRange);
                }
            }
            if (!eligible) {
                continue;
            }
            if (id == "cleanShadows") {
                cleanShadowCandidateGuidance =
                    guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                cleanShadowCandidateGenerated = true;
            } else if (id == "preserveTexture") {
                preserveTextureCandidateGuidance =
                    guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), payload.autoGuidance);
                preserveTextureCandidateGenerated = true;
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload regionalEvidencePayload = payload;
    const std::string regionalRenderedCandidateId =
        selectedCandidateId.empty() ? std::string("base") : selectedCandidateId;
    regionalEvidencePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
    regionalEvidencePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    regionalEvidencePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] =
        nlohmann::json::array({
            {
                { "id", regionalRenderedCandidateId },
                { "label", "Selected Regional Evidence Fixture" },
                { "success", true },
                { "renderScore", 0.62f },
                { "metrics", {
                    { "meanLuma", 0.33f },
                    { "medianLuma", 0.28f },
                    { "p10Luma", 0.05f },
                    { "p90Luma", 0.78f },
                    { "shadowFraction", 0.42f },
                    { "highlightFraction", 0.28f },
                    { "clippedFraction", 0.008f },
                    { "contrastSpan", 0.34f },
                    { "meanRed", 0.34f },
                    { "meanGreen", 0.33f },
                    { "meanBlue", 0.32f },
                    { "warmCoolBias", 0.03f },
                    { "magentaGreenBias", 0.01f },
                    { "channelImbalance", 0.04f },
                    { "colorCastRisk", 0.0f },
                    { "meanSaturation", 0.18f },
                    { "lowSaturationFraction", 0.72f },
                    { "edgeContrast", 0.46f },
                    { "haloRiskFraction", 0.12f },
                    { "shadowTextureRisk", 0.45f },
                    { "localMeanLuma3x3", std::array<float, 9>{ 0.09f, 0.18f, 0.82f, 0.16f, 0.26f, 0.74f, 0.11f, 0.21f, 0.67f } },
                    { "localContrastSpan3x3", std::array<float, 9>{ 0.28f, 0.44f, 0.90f, 0.36f, 0.52f, 0.84f, 0.32f, 0.40f, 0.78f } },
                    { "localDamageRiskScore3x3", std::array<float, 9>{ 0.38f, 0.48f, 0.80f, 0.44f, 0.56f, 0.76f, 0.36f, 0.42f, 0.70f } },
                    { "localLumaSpread", 0.73f },
                    { "localEvSpreadStops", 3.20f },
                    { "localEvConflict", 0.68f },
                    { "localContrastPeak", 0.90f },
                    { "localShadowPressure", 0.66f },
                    { "localHighlightPressure", 0.74f },
                    { "localDamageRiskMean", 0.54f },
                    { "localDamageRiskPeak", 0.80f },
                    { "localDamageRiskPeakTile", 2 },
                    { "localExposureHighlightCrowding", 0.62f },
                    { "localExposureShadowCrowding", 0.58f },
                    { "localExposureHaloStress", 0.64f },
                    { "localExposureFlatnessRisk", 0.42f },
                    { "localExposureDamageRisk", 0.60f },
                    { "centerMeanLuma", 0.26f },
                    { "centerShadowFraction", 0.44f },
                    { "centerHighlightFraction", 0.32f },
                    { "subjectCenterPrior", 0.74f },
                    { "subjectReadabilityPressure", 0.44f },
                    { "subjectProtectionPressure", 0.22f },
                    { "subjectMoodPreservationPressure", 0.18f },
                    { "subjectImportanceConfidence", 0.68f }
                } }
            }
        });
    EditorModule::ApplyDevelopAutoSolve(regionalEvidencePayload, metadata, true);
    const nlohmann::json regionalEvidence =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeRegionEvidence",
            nlohmann::json::object());
    regionalEvidenceDiagnosticsWritten =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeRegionEvidenceVersion",
            std::string()) == "DynamicRangeRegionEvidenceV1" &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeRegionEvidenceValid",
            false) &&
        regionalEvidence.value("version", std::string()) == "DynamicRangeRegionEvidenceV1" &&
        regionalEvidence.value("source", std::string()) == "selectedRenderedCandidate" &&
        regionalEvidence.value("localEvSpreadStops", 0.0f) > 1.0f &&
        regionalEvidence.value("localEvConflict", 0.0f) > 0.20f &&
        regionalEvidence.value("subjectCenterPrior", 0.0f) > 0.50f &&
        regionalEvidence.value("subjectReadabilityPressure", 0.0f) > 0.30f &&
        regionalEvidence.value("subjectImportanceConfidence", 0.0f) > 0.50f &&
        regionalEvidence.value("localExposureHighlightCrowding", 0.0f) > 0.40f &&
        regionalEvidence.value("localExposureShadowCrowding", 0.0f) > 0.30f &&
        regionalEvidence.value("localExposureHaloStress", 0.0f) > 0.40f &&
        regionalEvidence.value("localExposureDamageRisk", 0.0f) > 0.35f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalHighlightHotspotRisk",
            -1.0f) > 0.20f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalRangeConflict",
            -1.0f) > 0.20f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalEvConflict",
            -1.0f) > 0.20f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalExposureDamageRisk",
            -1.0f) > 0.35f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalExposureHaloStress",
            -1.0f) > 0.40f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoDynamicRangeLocalEvSpreadStops",
            -1.0f) > 1.0f;
    const nlohmann::json regionalCandidateSolves =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (regionalCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "haloSafeLocalRange") {
                continue;
            }
            haloSafeLocalRangeGenerated = true;
            const std::string status = candidate.value("status", std::string());
            haloSafeLocalRangeEligible =
                haloSafeLocalRangeEligible ||
                status == "selected" ||
                status == "survivor";
            haloSafeLocalRangeGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    regionalEvidencePayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            haloSafeLocalRangeHumanReadable =
                haloSafeLocalRangeHumanReadable ||
                (label.find("Halo") != std::string::npos &&
                 label.find("Local") != std::string::npos &&
                 reason.find("halo") != std::string::npos &&
                 reason.find("smooth-gradient") != std::string::npos);
            haloSafeLocalRangeDiagnosticsWritten =
                haloSafeLocalRangeDiagnosticsWritten ||
                (signals.value("localHaloSafetySignal", -1.0f) > 0.28f &&
                 dimensions.value("localHaloSafety", -1.0f) > 0.54f &&
                 dimensions.value("localArtifactSafety", -1.0f) > 0.40f &&
                 changes.value("dynamicRangeDelta", 0.0f) < -0.04f &&
                 changes.value("highlightGuardDelta", 0.0f) > 0.08f);
        }
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "localRangeGuard") {
                continue;
            }
            localRangeGuardGenerated = true;
            const std::string status = candidate.value("status", std::string());
            localRangeGuardEligible =
                localRangeGuardEligible ||
                status == "selected" ||
                status == "survivor";
            localRangeGuardGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    regionalEvidencePayload.autoGuidance);
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            localRangeGuardDiagnosticsWritten =
                localRangeGuardDiagnosticsWritten ||
                signals.value("regionEvidenceValid", false) &&
                    signals.value("localRangeConflict", 0.0f) > 0.20f &&
                    signals.value("localEvConflict", 0.0f) > 0.20f &&
                    signals.value("localEvSpreadStops", 0.0f) > 1.0f &&
                    risks.value("localRangeConflict", 0.0f) > 0.20f &&
                    risks.value("localEvConflict", 0.0f) > 0.20f;
        }
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "shadowNoiseFloor") {
                continue;
            }
            shadowNoiseFloorGenerated = true;
            const std::string status = candidate.value("status", std::string());
            shadowNoiseFloorEligible =
                shadowNoiseFloorEligible ||
                status == "selected" ||
                status == "survivor";
            shadowNoiseFloorGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    regionalEvidencePayload.autoGuidance);
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            shadowNoiseFloorDiagnosticsWritten =
                shadowNoiseFloorDiagnosticsWritten ||
                signals.value("regionEvidenceValid", false) &&
                    signals.value("shadowNoiseLiftRisk", 0.0f) > 0.20f &&
                    dimensions.value("shadowCleanliness", 0.0f) > 0.50f &&
                    risks.value("shadowNoiseRisk", 0.0f) > 0.20f;
        }
    }
    const nlohmann::json subjectSceneIntent =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const bool subjectSceneIntentDiagnosticsWritten =
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        subjectSceneIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "deferred" &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneAutomaticOnly",
            false) &&
        !regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            true) &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneAutomaticConfidence",
            0.0f) > 0.50f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneCenterPrior",
            0.0f) > 0.50f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneReadabilityPressure",
            0.0f) > 0.30f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSubjectPriority",
            0.0f) > 0.50f &&
        regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImproveReadability",
            0.0f) > 0.45f &&
        std::abs(regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSubjectSceneAxis",
            99.0f)) <= 1.0f &&
        std::abs(regionalEvidencePayload.integratedToneLayerJson.value(
            "autoSubjectSceneMoodReadabilityAxis",
            99.0f)) <= 1.0f;
    bool subjectSceneIntentScoreComponentsWritten = false;
    bool subjectSceneIntentBiasesScoring = false;
    if (regionalCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : regionalCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            const bool hasSubjectScoreShape =
                candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "notAvailable" &&
                signals.value("subjectAutomaticConfidence", 0.0f) > 0.50f &&
                signals.value("subjectReadabilityPressure", 0.0f) > 0.30f &&
                dimensions.contains("subjectPriorityFit") &&
                dimensions.contains("subjectReadabilityFit") &&
                dimensions.contains("subjectProtectionFit") &&
                dimensions.contains("subjectMoodFit") &&
                risks.contains("subjectOverLiftRisk") &&
                risks.contains("subjectProtectionTradeoffRisk");
            subjectSceneIntentScoreComponentsWritten =
                subjectSceneIntentScoreComponentsWritten || hasSubjectScoreShape;
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            subjectSceneIntentBiasesScoring =
                subjectSceneIntentBiasesScoring ||
                (eligible &&
                 hasSubjectScoreShape &&
                (signals.value("subjectReadabilityBias", 0.0f) > 0.02f ||
                  dimensions.value("subjectReadabilityFit", 0.0f) > 0.55f ||
                  dimensions.value("subjectPriorityFit", 0.0f) > 0.55f));
        }
    }
    EditorNodeGraph::RawDevelopPayload userSubjectIntentPayload = regionalEvidencePayload;
    userSubjectIntentPayload.autoGuidance.subjectSceneBias = 0.68f;
    userSubjectIntentPayload.autoGuidance.moodReadabilityBias = 0.46f;
    EditorModule::ApplyDevelopAutoSolve(userSubjectIntentPayload, metadata, true);
    const nlohmann::json userSubjectSceneIntent =
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const bool userSubjectSceneIntentDiagnosticsWritten =
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        userSubjectSceneIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        userSubjectSceneIntent.value("userGuidanceStatus", std::string()) == "intentControls" &&
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) == "intentControls" &&
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        !userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneAutomaticOnly",
            true) &&
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStrength",
            0.0f) > 0.60f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserSubjectSceneBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserMoodReadabilityBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.moodReadabilityBias) < 0.0001f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectSceneBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoRequestedMoodReadabilityBias",
            -99.0f) - userSubjectIntentPayload.autoGuidance.moodReadabilityBias) < 0.0001f &&
        userSubjectSceneIntent.value("id", std::string()).find("userGuided") == 0;
    bool userSubjectSceneIntentScoreComponentsWritten = false;
    const nlohmann::json userSubjectCandidateSolves =
        userSubjectIntentPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    bool subjectReadableMidsGenerated = false;
    bool subjectReadableMidsEligible = false;
    bool subjectReadableMidsHumanReadable = false;
    bool subjectReadableMidsDiagnosticsWritten = false;
    EditorNodeGraph::DevelopAutoGuidance subjectReadableMidsGuidance =
        userSubjectIntentPayload.autoGuidance;
    if (userSubjectCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : userSubjectCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const std::string status = candidate.value("status", std::string());
            const nlohmann::json candidateGuidance =
                candidate.value("guidance", nlohmann::json::object());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json risks =
                scoreComponents.value("risks", nlohmann::json::object());
            userSubjectSceneIntentScoreComponentsWritten =
                userSubjectSceneIntentScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "intentControls" &&
                 candidateSubjectIntent.value("userGuidanceActive", false) &&
                 candidateSubjectIntent.value("userGuidanceStrength", 0.0f) > 0.60f &&
                 signals.value("subjectUserGuidanceStrength", 0.0f) > 0.60f &&
                 signals.value("subjectUserSubjectSceneBias", 0.0f) > 0.60f &&
                 signals.value("subjectUserMoodReadabilityBias", 0.0f) > 0.40f &&
                 std::abs(candidateGuidance.value("subjectSceneBias", -99.0f) -
                     userSubjectIntentPayload.autoGuidance.subjectSceneBias) < 0.0001f &&
                 std::abs(candidateGuidance.value("moodReadabilityBias", -99.0f) -
                     userSubjectIntentPayload.autoGuidance.moodReadabilityBias) < 0.0001f);
            if (id == "subjectReadableMids") {
                subjectReadableMidsGenerated = true;
                subjectReadableMidsEligible =
                    subjectReadableMidsEligible ||
                    status == "selected" ||
                    status == "survivor";
                subjectReadableMidsHumanReadable =
                    candidate.value("label", std::string()).find("Subject") != std::string::npos &&
                    candidate.value("reason", std::string()).find("subject-priority") != std::string::npos;
                subjectReadableMidsGuidance =
                    guidanceFromCandidateJson(candidateGuidance, userSubjectIntentPayload.autoGuidance);
                subjectReadableMidsDiagnosticsWritten =
                    subjectReadableMidsDiagnosticsWritten ||
                    candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                    candidateSubjectIntent.value("userGuidanceActive", false) &&
                    signals.value("subjectUserSubjectSceneBias", 0.0f) > 0.60f &&
                    signals.value("subjectUserMoodReadabilityBias", 0.0f) > 0.40f &&
                    dimensions.value("subjectReadabilityFit", 0.0f) > 0.58f &&
                    dimensions.value("subjectPriorityFit", 0.0f) > 0.56f &&
                    risks.contains("subjectOverLiftRisk");
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload subjectImportancePayload = regionalEvidencePayload;
    subjectImportancePayload.subjectImportance.enabled = true;
    subjectImportancePayload.subjectImportance.showOverlay = true;
    subjectImportancePayload.subjectImportance.overlayOpacity = 0.42f;
    subjectImportancePayload.subjectImportance.nextRegionId = 8;
    EditorNodeGraph::DevelopSubjectImportanceRegion subjectRegion;
    subjectRegion.id = 7;
    subjectRegion.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    subjectRegion.enabled = true;
    subjectRegion.centerX = 0.46f;
    subjectRegion.centerY = 0.54f;
    subjectRegion.radiusX = 0.28f;
    subjectRegion.radiusY = 0.22f;
    subjectRegion.feather = 0.40f;
    subjectRegion.strength = 0.86f;
    subjectImportancePayload.subjectImportance.regions.push_back(subjectRegion);
    EditorModule::ApplyDevelopAutoSolve(subjectImportancePayload, metadata, true);
    const nlohmann::json subjectImportanceIntent =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const nlohmann::json subjectImportanceSolveNotes =
        subjectImportanceIntent.value("solveNotes", nlohmann::json::array());
    const std::uint64_t subjectImportanceFingerprintA =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    EditorNodeGraph::RawDevelopPayload subjectImportanceChangedPayload = subjectImportancePayload;
    subjectImportanceChangedPayload.subjectImportance.regions[0].strength = 0.28f;
    EditorModule::ApplyDevelopAutoSolve(subjectImportanceChangedPayload, metadata, true);
    const std::uint64_t subjectImportanceFingerprintB =
        subjectImportanceChangedPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    EditorNodeGraph::RawDevelopPayload subjectImportanceVisualPayload = regionalEvidencePayload;
    subjectImportanceVisualPayload.subjectImportance = subjectImportancePayload.subjectImportance;
    subjectImportanceVisualPayload.subjectImportance.showOverlay = false;
    subjectImportanceVisualPayload.subjectImportance.overlayOpacity = 0.93f;
    subjectImportanceVisualPayload.subjectImportance.showInterpretedMapOverlay = true;
    subjectImportanceVisualPayload.subjectImportance.interpretedMapOpacity = 0.81f;
    subjectImportanceVisualPayload.subjectImportance.showRefinedMapOverlay = true;
    subjectImportanceVisualPayload.subjectImportance.refinedMapOpacity = 0.76f;
    EditorModule::ApplyDevelopAutoSolve(subjectImportanceVisualPayload, metadata, true);
    const std::uint64_t subjectImportanceVisualFingerprint =
        subjectImportanceVisualPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    bool subjectImportanceScoreComponentsWritten = false;
    const nlohmann::json subjectImportanceCandidateSolves =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (subjectImportanceCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : subjectImportanceCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json candidateImportanceMap =
                candidateSubjectIntent.value("importanceMap", nlohmann::json::object());
            const nlohmann::json candidateRefinedMap =
                candidateSubjectIntent.value("refinedImportanceMap", nlohmann::json::object());
            const nlohmann::json candidateSolveNotes =
                candidateSubjectIntent.value("solveNotes", nlohmann::json::array());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            subjectImportanceScoreComponentsWritten =
                subjectImportanceScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("solveNotesVersion", std::string()) == "SubjectImportanceSolveNotesV1" &&
                 candidateSolveNotes.is_array() &&
                 !candidateSolveNotes.empty() &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "importanceRegions" &&
                 candidateImportanceMap.value("version", std::string()) == "SubjectImportanceMapV1" &&
                 candidateImportanceMap.value("status", std::string()) == "interpretedUserMarks" &&
                 candidateRefinedMap.value("version", std::string()) == "SubjectRefinedMapV1" &&
                 candidateRefinedMap.value("status", std::string()) == "refinedUserMarks" &&
                 signals.value("subjectImportanceRegionCount", 0) == 1 &&
                 signals.value("subjectImportanceReveal", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapRevealCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapConfidence", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapReadabilityCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapConfidence", 0.0f) > 0.0f);
        }
    }
    const bool subjectImportanceGuidanceDiagnosticsWritten =
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        subjectImportanceIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        subjectImportanceIntent.value("solveNotesVersion", std::string()) == "SubjectImportanceSolveNotesV1" &&
        subjectImportanceSolveNotes.is_array() &&
        !subjectImportanceSolveNotes.empty() &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSolveNotesVersion",
            std::string()) == "SubjectImportanceSolveNotesV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneSolveNoteCount",
            0) > 0 &&
        !subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectScenePrimarySolveNote",
            std::string()).empty() &&
        subjectImportanceIntent.value("userGuidanceStatus", std::string()) == "importanceRegions" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "regionGuidanceActive" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) == "importanceRegions" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceRegionCount",
            0) == 1 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceRegionCount",
            0) == 1 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            -1) == 0 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            -1) == 0 &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceReveal",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrength",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapStatus",
            std::string()) == "interpretedUserMarks" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapActive",
            false) &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapRevealCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapConfidence",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapVersion",
            std::string()) == "SubjectRefinedMapV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapStatus",
            std::string()) == "refinedUserMarks" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapActive",
            false) &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapReadabilityCoverage",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapConfidence",
            0.0f) > 0.0f &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectImportancePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceMapCoverage",
            0.0f) > 0.0f &&
        subjectImportanceFingerprintA != 0 &&
        subjectImportanceFingerprintB != 0 &&
        subjectImportanceFingerprintA != subjectImportanceFingerprintB &&
        subjectImportanceVisualFingerprint == subjectImportanceFingerprintA &&
        subjectImportanceScoreComponentsWritten;
    EditorNodeGraph::RawDevelopPayload subjectBrushPayload = regionalEvidencePayload;
    subjectBrushPayload.subjectImportance.enabled = true;
    subjectBrushPayload.subjectImportance.showOverlay = true;
    subjectBrushPayload.subjectImportance.brushEnabled = true;
    subjectBrushPayload.subjectImportance.brushMode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    subjectBrushPayload.subjectImportance.brushRadius = 0.052f;
    subjectBrushPayload.subjectImportance.brushFeather = 0.42f;
    subjectBrushPayload.subjectImportance.brushStrength = 0.88f;
    subjectBrushPayload.subjectImportance.activeStrokeId = 11;
    subjectBrushPayload.subjectImportance.nextStrokeId = 12;
    EditorNodeGraph::DevelopSubjectImportanceStroke subjectStroke;
    subjectStroke.id = 11;
    subjectStroke.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    subjectStroke.enabled = true;
    subjectStroke.subtract = false;
    subjectStroke.radius = 0.052f;
    subjectStroke.feather = 0.42f;
    subjectStroke.strength = 0.88f;
    subjectStroke.points.push_back({ 0.35f, 0.44f });
    subjectStroke.points.push_back({ 0.44f, 0.51f });
    subjectStroke.points.push_back({ 0.56f, 0.58f });
    subjectBrushPayload.subjectImportance.strokes.push_back(subjectStroke);
    EditorModule::ApplyDevelopAutoSolve(subjectBrushPayload, metadata, true);
    const nlohmann::json subjectBrushIntent =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    const std::uint64_t subjectBrushFingerprintA =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    EditorNodeGraph::RawDevelopPayload subjectBrushChangedPayload = subjectBrushPayload;
    subjectBrushChangedPayload.subjectImportance.strokes[0].points[1].x = 0.64f;
    EditorModule::ApplyDevelopAutoSolve(subjectBrushChangedPayload, metadata, true);
    const std::uint64_t subjectBrushFingerprintB =
        subjectBrushChangedPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    bool subjectBrushScoreComponentsWritten = false;
    const nlohmann::json subjectBrushCandidateSolves =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (subjectBrushCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : subjectBrushCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json candidateImportanceMap =
                candidateSubjectIntent.value("importanceMap", nlohmann::json::object());
            const nlohmann::json candidateRefinedMap =
                candidateSubjectIntent.value("refinedImportanceMap", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            subjectBrushScoreComponentsWritten =
                subjectBrushScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
                 candidateImportanceMap.value("version", std::string()) == "SubjectImportanceMapV1" &&
                 candidateImportanceMap.value("status", std::string()) == "interpretedUserMarks" &&
                 candidateRefinedMap.value("version", std::string()) == "SubjectRefinedMapV1" &&
                 candidateRefinedMap.value("status", std::string()) == "refinedUserMarks" &&
                 signals.value("subjectImportanceStrokeCount", 0) == 1 &&
                 signals.value("subjectImportanceReveal", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapRevealCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapConfidence", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapCoverage", 0.0f) > 0.0f &&
                 signals.value("subjectRefinedMapConfidence", 0.0f) > 0.0f);
        }
    }
    const bool subjectBrushGuidanceDiagnosticsWritten =
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntentVersion",
            std::string()) == "SubjectSceneIntentV1" &&
        subjectBrushIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        subjectBrushIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "brushStrokesActiveEdgeRefineDeferred" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) == "importanceBrush" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceRegionCount",
            -1) == 0 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            0) == 1 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceRegionCount",
            -1) == 0 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            0) == 1 &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceReveal",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrength",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapStatus",
            std::string()) == "interpretedUserMarks" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapCoverage",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapPeak",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapConfidence",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapVersion",
            std::string()) == "SubjectRefinedMapV1" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapStatus",
            std::string()) == "refinedUserMarks" &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapCoverage",
            0.0f) > 0.0f &&
        subjectBrushPayload.integratedToneLayerJson.value(
            "autoSubjectSceneRefinedMapConfidence",
            0.0f) > 0.0f &&
        subjectBrushFingerprintA != 0 &&
        subjectBrushFingerprintB != 0 &&
        subjectBrushFingerprintA != subjectBrushFingerprintB &&
        subjectBrushScoreComponentsWritten;
    EditorNodeGraph::RawDevelopPayload subjectBrushDisabledPayload = regionalEvidencePayload;
    subjectBrushDisabledPayload.subjectImportance = subjectBrushPayload.subjectImportance;
    subjectBrushDisabledPayload.subjectImportance.strokes[0].enabled = false;
    EditorModule::ApplyDevelopAutoSolve(subjectBrushDisabledPayload, metadata, true);
    const std::uint64_t subjectBrushDisabledFingerprint =
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoCandidateContextFingerprint",
            static_cast<std::uint64_t>(0));
    const bool subjectBrushDisabledIgnored =
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            -1) == 0 &&
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            -1) == 0 &&
        !subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            true) &&
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStatus",
            std::string()) != "importanceBrush" &&
        !subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapActive",
            true) &&
        subjectBrushDisabledPayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapCoverage",
            1.0f) <= 0.001f &&
        subjectBrushDisabledFingerprint != 0 &&
        subjectBrushDisabledFingerprint != subjectBrushFingerprintA;

    EditorNodeGraph::RawDevelopPayload subjectBrushReducePayload = regionalEvidencePayload;
    subjectBrushReducePayload.subjectImportance = subjectBrushPayload.subjectImportance;
    subjectBrushReducePayload.subjectImportance.strokes[0].subtract = true;
    subjectBrushReducePayload.subjectImportance.strokes[0].mode =
        EditorNodeGraph::DevelopSubjectImportanceMode::Ignore;
    EditorModule::ApplyDevelopAutoSolve(subjectBrushReducePayload, metadata, true);
    const nlohmann::json subjectBrushReduceIntent =
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneIntent",
            nlohmann::json::object());
    bool subjectBrushReduceScoreComponentsWritten = false;
    const nlohmann::json subjectBrushReduceCandidateSolves =
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (subjectBrushReduceCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : subjectBrushReduceCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json candidateImportanceMap =
                candidateSubjectIntent.value("importanceMap", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            subjectBrushReduceScoreComponentsWritten =
                subjectBrushReduceScoreComponentsWritten ||
                (candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                 candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
                 candidateImportanceMap.value("version", std::string()) == "SubjectImportanceMapV1" &&
                 candidateImportanceMap.value("status", std::string()) == "interpretedUserMarks" &&
                 signals.value("subjectImportanceStrokeCount", 0) == 1 &&
                 signals.value("subjectImportanceIgnore", 0.0f) > 0.0f &&
                 signals.value("subjectImportanceMapLowPriorityCoverage", 0.0f) > 0.0f);
        }
    }
    const bool subjectBrushReduceGuidanceDiagnosticsWritten =
        subjectBrushReduceIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
        subjectBrushReduceIntent.value("userGuidanceStatus", std::string()) == "importanceBrush" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneBrushStatus",
            std::string()) == "brushStrokesActiveEdgeRefineDeferred" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceActive",
            false) &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceStrokeCount",
            0) == 1 &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoRequestedSubjectImportanceStrokeCount",
            0) == 1 &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceIgnore",
            0.0f) > 0.0f &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneUserGuidanceStrength",
            0.0f) > 0.0f &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapVersion",
            std::string()) == "SubjectImportanceMapV1" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapStatus",
            std::string()) == "interpretedUserMarks" &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapLowPriorityCoverage",
            0.0f) > 0.0f &&
        subjectBrushReducePayload.integratedToneLayerJson.value(
            "autoSubjectSceneImportanceMapConfidence",
            0.0f) > 0.0f &&
        subjectBrushReduceScoreComponentsWritten;
    EditorNodeGraph::RawDevelopPayload sceneMoodIntentPayload = regionalEvidencePayload;
    sceneMoodIntentPayload.autoGuidance.subjectSceneBias = -0.62f;
    sceneMoodIntentPayload.autoGuidance.moodReadabilityBias = -0.56f;
    EditorModule::ApplyDevelopAutoSolve(sceneMoodIntentPayload, metadata, true);
    const nlohmann::json sceneMoodCandidateSolves =
        sceneMoodIntentPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    bool sceneMoodPreservationGenerated = false;
    bool sceneMoodPreservationEligible = false;
    bool sceneMoodPreservationHumanReadable = false;
    bool sceneMoodPreservationDiagnosticsWritten = false;
    EditorNodeGraph::DevelopAutoGuidance sceneMoodPreservationGuidance =
        sceneMoodIntentPayload.autoGuidance;
    if (sceneMoodCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : sceneMoodCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "sceneMoodPreservation") {
                continue;
            }
            sceneMoodPreservationGenerated = true;
            const std::string status = candidate.value("status", std::string());
            sceneMoodPreservationEligible =
                sceneMoodPreservationEligible ||
                status == "selected" ||
                status == "survivor";
            sceneMoodPreservationHumanReadable =
                candidate.value("label", std::string()).find("Mood") != std::string::npos &&
                candidate.value("reason", std::string()).find("scene-integrity") != std::string::npos;
            sceneMoodPreservationGuidance =
                guidanceFromCandidateJson(
                    candidate.value("guidance", nlohmann::json::object()),
                    sceneMoodIntentPayload.autoGuidance);
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json candidateSubjectIntent =
                scoreComponents.value("subjectSceneIntent", nlohmann::json::object());
            const nlohmann::json signals =
                scoreComponents.value("signals", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            sceneMoodPreservationDiagnosticsWritten =
                sceneMoodPreservationDiagnosticsWritten ||
                candidateSubjectIntent.value("version", std::string()) == "SubjectSceneIntentV1" &&
                candidateSubjectIntent.value("userGuidanceStatus", std::string()) == "intentControls" &&
                candidateSubjectIntent.value("userGuidanceActive", false) &&
                signals.value("subjectUserSubjectSceneBias", 0.0f) < -0.50f &&
                signals.value("subjectUserMoodReadabilityBias", 0.0f) < -0.45f &&
                dimensions.value("subjectMoodFit", 0.0f) > 0.58f;
        }
    }
    RenderGraphRawDevelopPayload baseCandidateRenderPayload;
    baseCandidateRenderPayload.settings = payload.settings;
    baseCandidateRenderPayload.scenePrepEnabled = payload.scenePrepEnabled;
    baseCandidateRenderPayload.scenePrepSettings = payload.scenePrepSettings;
    baseCandidateRenderPayload.integratedToneEnabled = payload.integratedToneEnabled;
    baseCandidateRenderPayload.integratedToneLayerJson = payload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentRenderedGuidance =
        guidanceFromToneJson(payload.integratedToneLayerJson, payload.autoGuidance);
    currentRenderedGuidance.intent = payload.autoGuidance.intent;
    RenderGraphRawDevelopPayload regionalBaseCandidateRenderPayload;
    regionalBaseCandidateRenderPayload.settings = regionalEvidencePayload.settings;
    regionalBaseCandidateRenderPayload.scenePrepEnabled = regionalEvidencePayload.scenePrepEnabled;
    regionalBaseCandidateRenderPayload.scenePrepSettings = regionalEvidencePayload.scenePrepSettings;
    regionalBaseCandidateRenderPayload.integratedToneEnabled = regionalEvidencePayload.integratedToneEnabled;
    regionalBaseCandidateRenderPayload.integratedToneLayerJson =
        regionalEvidencePayload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentRegionalRenderedGuidance =
        guidanceFromToneJson(
            regionalEvidencePayload.integratedToneLayerJson,
            regionalEvidencePayload.autoGuidance);
    currentRegionalRenderedGuidance.intent = regionalEvidencePayload.autoGuidance.intent;
    RenderGraphRawDevelopPayload userSubjectBaseCandidateRenderPayload;
    userSubjectBaseCandidateRenderPayload.settings = userSubjectIntentPayload.settings;
    userSubjectBaseCandidateRenderPayload.scenePrepEnabled = userSubjectIntentPayload.scenePrepEnabled;
    userSubjectBaseCandidateRenderPayload.scenePrepSettings = userSubjectIntentPayload.scenePrepSettings;
    userSubjectBaseCandidateRenderPayload.integratedToneEnabled = userSubjectIntentPayload.integratedToneEnabled;
    userSubjectBaseCandidateRenderPayload.integratedToneLayerJson =
        userSubjectIntentPayload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentUserSubjectRenderedGuidance =
        guidanceFromToneJson(
            userSubjectIntentPayload.integratedToneLayerJson,
            userSubjectIntentPayload.autoGuidance);
    currentUserSubjectRenderedGuidance.intent = userSubjectIntentPayload.autoGuidance.intent;
    RenderGraphRawDevelopPayload sceneMoodBaseCandidateRenderPayload;
    sceneMoodBaseCandidateRenderPayload.settings = sceneMoodIntentPayload.settings;
    sceneMoodBaseCandidateRenderPayload.scenePrepEnabled = sceneMoodIntentPayload.scenePrepEnabled;
    sceneMoodBaseCandidateRenderPayload.scenePrepSettings = sceneMoodIntentPayload.scenePrepSettings;
    sceneMoodBaseCandidateRenderPayload.integratedToneEnabled = sceneMoodIntentPayload.integratedToneEnabled;
    sceneMoodBaseCandidateRenderPayload.integratedToneLayerJson =
        sceneMoodIntentPayload.integratedToneLayerJson;
    EditorNodeGraph::DevelopAutoGuidance currentSceneMoodRenderedGuidance =
        guidanceFromToneJson(
            sceneMoodIntentPayload.integratedToneLayerJson,
            sceneMoodIntentPayload.autoGuidance);
    currentSceneMoodRenderedGuidance.intent = sceneMoodIntentPayload.autoGuidance.intent;
    const RenderGraphRawDevelopPayload cleanProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            cleanShadowCandidateGuidance,
            "cleanShadows",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload textureProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            preserveTextureCandidateGuidance,
            "preserveTexture",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload protectedMidsPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            highlightProtectedMidsGuidance,
            "highlightProtectedMids",
            payload.autoGuidance.intent);
    const std::string finishToneProbeRenderId =
        finishToneProbeId.empty() ? std::string("toneSofterRolloff") : finishToneProbeId;
    const RenderGraphRawDevelopPayload finishToneProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            finishToneProbeGuidance,
            finishToneProbeRenderId,
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload brightHighlightRolloffPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            brightHighlightRolloffGuidance,
            "brightHighlightRolloff",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload luminousHighlightAnchorPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            luminousHighlightAnchorGuidance,
            "luminousHighlightAnchor",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload localRangeGuardPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            regionalBaseCandidateRenderPayload,
            currentRegionalRenderedGuidance,
            localRangeGuardGuidance,
            "localRangeGuard",
            regionalEvidencePayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload haloSafeLocalRangePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            regionalBaseCandidateRenderPayload,
            currentRegionalRenderedGuidance,
            haloSafeLocalRangeGuidance,
            "haloSafeLocalRange",
            regionalEvidencePayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload shadowNoiseFloorPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            regionalBaseCandidateRenderPayload,
            currentRegionalRenderedGuidance,
            shadowNoiseFloorGuidance,
            "shadowNoiseFloor",
            regionalEvidencePayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload subjectReadableMidsPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            userSubjectBaseCandidateRenderPayload,
            currentUserSubjectRenderedGuidance,
            subjectReadableMidsGuidance,
            "subjectReadableMids",
            userSubjectIntentPayload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload sceneMoodPreservationPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            sceneMoodBaseCandidateRenderPayload,
            currentSceneMoodRenderedGuidance,
            sceneMoodPreservationGuidance,
            "sceneMoodPreservation",
            sceneMoodIntentPayload.autoGuidance.intent);
    const std::string whiteBalanceProbeRenderId =
        whiteBalanceProbeId.empty() ? std::string("wbDaylightCorrection") : whiteBalanceProbeId;
    const RenderGraphRawDevelopPayload whiteBalanceProbePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            baseCandidateRenderPayload,
            currentRenderedGuidance,
            whiteBalanceProbeGuidance,
            whiteBalanceProbeRenderId,
            payload.autoGuidance.intent);
    RenderGraphRawDevelopPayload stageConstraintBasePayload = baseCandidateRenderPayload;
    stageConstraintBasePayload.settings.exposureStops = 0.75f;
    stageConstraintBasePayload.settings.highlightStrength = 0.32f;
    stageConstraintBasePayload.settings.highlightThreshold = 0.95f;
    stageConstraintBasePayload.scenePrepSettings.strength = 0.55f;
    stageConstraintBasePayload.scenePrepSettings.maxEvBias = 0.35f;
    stageConstraintBasePayload.scenePrepSettings.minEvBias = -0.35f;
    stageConstraintBasePayload.scenePrepSettings.baseEvBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.highlightProtectionBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.noiseProtectionBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.shadowLiftLimitBias = 0.0f;
    stageConstraintBasePayload.scenePrepSettings.wellExposedTargetBias = 0.0f;
    EditorNodeGraph::DevelopAutoGuidance stageConstraintCurrentGuidance = payload.autoGuidance;
    stageConstraintCurrentGuidance.autoStrength = 0.50f;
    stageConstraintCurrentGuidance.exposureBias = 0.0f;
    stageConstraintCurrentGuidance.dynamicRange = 0.0f;
    stageConstraintCurrentGuidance.shadowLift = 0.0f;
    stageConstraintCurrentGuidance.highlightGuard = 0.0f;
    stageConstraintCurrentGuidance.highlightCharacter = 0.0f;
    stageConstraintCurrentGuidance.contrastBias = 0.0f;
    stageConstraintCurrentGuidance.intent = payload.autoGuidance.intent;
    EditorModule::NormalizeDevelopAutoGuidance(stageConstraintCurrentGuidance);
    EditorNodeGraph::DevelopAutoGuidance scenePrepStageGuidance = stageConstraintCurrentGuidance;
    scenePrepStageGuidance.exposureBias = 0.36f;
    scenePrepStageGuidance.dynamicRange = 0.34f;
    scenePrepStageGuidance.shadowLift = 0.28f;
    scenePrepStageGuidance.highlightGuard = 0.18f;
    EditorModule::NormalizeDevelopAutoGuidance(scenePrepStageGuidance);
    EditorNodeGraph::DevelopAutoGuidance finishToneStageGuidance = stageConstraintCurrentGuidance;
    finishToneStageGuidance.exposureBias = 0.30f;
    finishToneStageGuidance.dynamicRange = 0.24f;
    finishToneStageGuidance.shadowLift = 0.16f;
    finishToneStageGuidance.highlightGuard = 0.16f;
    finishToneStageGuidance.contrastBias = 0.46f;
    EditorModule::NormalizeDevelopAutoGuidance(finishToneStageGuidance);
    const RenderGraphRawDevelopPayload scenePrepStagePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            stageConstraintBasePayload,
            stageConstraintCurrentGuidance,
            scenePrepStageGuidance,
            "renderedLocalBrightenMids",
            payload.autoGuidance.intent);
    const RenderGraphRawDevelopPayload finishToneStagePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            stageConstraintBasePayload,
            stageConstraintCurrentGuidance,
            finishToneStageGuidance,
            "renderedLocalContrastShape",
            payload.autoGuidance.intent);
    const bool cleanupTextureCandidateRenderPayloadsDiverge =
        cleanShadowCandidateGenerated &&
        preserveTextureCandidateGenerated &&
        cleanProbePayload.settings.mosaicDenoise.enabled &&
        cleanProbePayload.settings.mosaicDenoise.lumaStrength >
            textureProbePayload.settings.mosaicDenoise.lumaStrength + 0.16f &&
        cleanProbePayload.settings.falseColorSuppression >
            textureProbePayload.settings.falseColorSuppression + 0.06f &&
        textureProbePayload.settings.preserveRealColor >
            cleanProbePayload.settings.preserveRealColor + 0.02f &&
        textureProbePayload.scenePrepSettings.textureSensitivity >
            cleanProbePayload.scenePrepSettings.textureSensitivity + 0.10f &&
        cleanProbePayload.integratedToneLayerJson.value("autoCandidateCleanupProbe", std::string()) == "cleanerShadows" &&
        textureProbePayload.integratedToneLayerJson.value("autoCandidateCleanupProbe", std::string()) == "preserveTexture";
    const bool highlightProtectedMidsRenderPayloadDiverges =
        highlightProtectedMidsGenerated &&
        protectedMidsPayload.settings.exposureStops <
            baseCandidateRenderPayload.settings.exposureStops - 0.05f &&
        std::abs(protectedMidsPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f) -
            highlightProtectedMidsGuidance.dynamicRange) < 0.0001f &&
        std::abs(protectedMidsPayload.integratedToneLayerJson.value("autoShadowBias", -99.0f) -
            highlightProtectedMidsGuidance.shadowLift) < 0.0001f &&
        std::abs(protectedMidsPayload.integratedToneLayerJson.value("autoHighlightBias", -99.0f) -
            highlightProtectedMidsGuidance.highlightGuard) < 0.0001f &&
        protectedMidsPayload.integratedToneLayerJson.value("autoCandidateRenderedProbeId", std::string()) == "highlightProtectedMids";
    const bool scenePrepCandidateStageConstrained =
        std::abs(scenePrepStagePayload.settings.exposureStops -
            stageConstraintBasePayload.settings.exposureStops) < 0.0001f &&
        std::abs(scenePrepStagePayload.settings.highlightStrength -
            stageConstraintBasePayload.settings.highlightStrength) < 0.0001f &&
        std::abs(scenePrepStagePayload.settings.highlightThreshold -
            stageConstraintBasePayload.settings.highlightThreshold) < 0.0001f &&
        scenePrepStagePayload.scenePrepSettings.baseEvBias >
            stageConstraintBasePayload.scenePrepSettings.baseEvBias + 0.05f &&
        scenePrepStagePayload.scenePrepSettings.maxEvBias >
            stageConstraintBasePayload.scenePrepSettings.maxEvBias + 0.20f &&
        scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        std::abs(scenePrepStagePayload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f) -
            scenePrepStageGuidance.exposureBias) < 0.0001f;
    const bool finishToneCandidateStageConstrained =
        std::abs(finishToneStagePayload.settings.exposureStops -
            stageConstraintBasePayload.settings.exposureStops) < 0.0001f &&
        std::abs(finishToneStagePayload.settings.highlightStrength -
            stageConstraintBasePayload.settings.highlightStrength) < 0.0001f &&
        std::abs(finishToneStagePayload.scenePrepSettings.baseEvBias -
            stageConstraintBasePayload.scenePrepSettings.baseEvBias) < 0.0001f &&
        std::abs(finishToneStagePayload.scenePrepSettings.maxEvBias -
            stageConstraintBasePayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        std::abs(finishToneStagePayload.scenePrepSettings.minEvBias -
            stageConstraintBasePayload.scenePrepSettings.minEvBias) < 0.0001f &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        std::abs(finishToneStagePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) -
            finishToneStageGuidance.contrastBias) < 0.0001f;
    const bool finishToneProbeRenderPayloadConstrained =
        finishToneProbeGenerated &&
        finishToneProbeEligible &&
        std::abs(finishToneProbePayload.settings.exposureStops -
            baseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(finishToneProbePayload.settings.highlightStrength -
            baseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        std::abs(finishToneProbePayload.scenePrepSettings.baseEvBias -
            baseCandidateRenderPayload.scenePrepSettings.baseEvBias) < 0.0001f &&
        std::abs(finishToneProbePayload.scenePrepSettings.maxEvBias -
            baseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        finishToneProbePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == finishToneProbeRenderId &&
        std::abs(finishToneProbePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) -
            finishToneProbeGuidance.contrastBias) < 0.0001f &&
        std::abs(finishToneProbePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) -
            finishToneProbeGuidance.highlightCharacter) < 0.0001f;
    const nlohmann::json dynamicRangeStrategy =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategy", nlohmann::json::object());
    const bool dynamicRangeStrategyDiagnosticsWritten =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyVersion", std::string()) == "DynamicRangeStrategyV1" &&
        dynamicRangeStrategy.value("version", std::string()) == "DynamicRangeStrategyV1" &&
        !payload.integratedToneLayerJson.value("autoDynamicRangeStrategyId", std::string()).empty() &&
        !payload.integratedToneLayerJson.value("autoDynamicRangeStrategyLabel", std::string()).empty() &&
        payload.integratedToneLayerJson.value("autoDynamicRangeHighlightImportance", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeShadowReadability", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeNoiseConstraint", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalHaloGuardNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeNaturalContrastGuardNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeBrightHighlightRolloffNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeBroadHighlightGuardNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeSpecularHighlightToleranceNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeShadowReadabilityLiftNeed", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeShadowNoiseFloorNeed", -1.0f) >= 0.0f;
    const nlohmann::json dynamicRangeStrategyMap =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMap", nlohmann::json::object());
    bool dynamicRangeStrategyMapScoreComponentsWritten = false;
    const nlohmann::json strategyMapCandidateSolves =
        payload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (strategyMapCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : strategyMapCandidateSolves) {
            if (!candidate.is_object()) {
                continue;
            }
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json scoreMap =
                scoreComponents.value("dynamicRangeStrategyMap", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            if (scoreMap.value("version", std::string()) == "DynamicRangeStrategyMapV1" &&
                dimensions.value("strategyHighlightFit", -1.0f) >= 0.0f &&
                dimensions.value("strategyShadowFit", -1.0f) >= 0.0f &&
                dimensions.value("strategyVisibleRangeFit", -1.0f) >= 0.0f &&
                dimensions.value("strategyNaturalContrastFit", -1.0f) >= 0.0f) {
                dynamicRangeStrategyMapScoreComponentsWritten = true;
                break;
            }
        }
    }
    const float strategyMapHighlightShadowAxis =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightShadowAxis", -99.0f);
    const float strategyMapContrastRangeAxis =
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapContrastRangeAxis", -99.0f);
    const bool dynamicRangeStrategyMapDiagnosticsWritten =
        dynamicRangeStrategyDiagnosticsWritten &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVersion", std::string()) == "DynamicRangeStrategyMapV1" &&
        dynamicRangeStrategyMap.value("version", std::string()) == "DynamicRangeStrategyMapV1" &&
        dynamicRangeStrategy.value("strategyMapVersion", std::string()) == "DynamicRangeStrategyMapV1" &&
        strategyMapHighlightShadowAxis >= -1.0f &&
        strategyMapHighlightShadowAxis <= 1.0f &&
        strategyMapContrastRangeAxis >= -1.0f &&
        strategyMapContrastRangeAxis <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightPriority", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightPriority", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapShadowVisibility", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapShadowVisibility", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapNaturalContrast", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapNaturalContrast", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVisibleRange", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVisibleRange", -1.0f) <= 1.0f &&
        std::abs(strategyMapHighlightShadowAxis -
            dynamicRangeStrategyMap.value("highlightShadowAxis", -98.0f)) < 0.0001f &&
        std::abs(strategyMapContrastRangeAxis -
            dynamicRangeStrategyMap.value("contrastRangeAxis", -98.0f)) < 0.0001f &&
        dynamicRangeStrategyMapScoreComponentsWritten;
    const nlohmann::json localExposureStrategy =
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategy", nlohmann::json::object());
    const std::string localExposureStrategyId =
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyId", std::string());
    const bool localExposureStrategyDiagnosticsWritten =
        dynamicRangeStrategyDiagnosticsWritten &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        localExposureStrategy.value("version", std::string()) == "LocalExposureStrategyV1" &&
        dynamicRangeStrategy.value("localExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        !localExposureStrategyId.empty() &&
        !payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyLabel", std::string()).empty() &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureRangeRedistribution", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureRangeRedistribution", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCompression", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCompression", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowOpening", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowOpening", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureNoiseGuard", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureNoiseGuard", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloGuard", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloGuard", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureTextureGuard", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureTextureGuard", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrengthTarget", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrengthTarget", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCrowding", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCrowding", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowCrowding", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowCrowding", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloStress", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloStress", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureFlatnessRisk", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureFlatnessRisk", -1.0f) <= 1.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureDamageRisk", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureDamageRisk", -1.0f) <= 1.0f;
    const bool localExposureStrategyAuthoredScenePrep =
        localExposureStrategyDiagnosticsWritten &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrategyId", std::string()) == localExposureStrategyId &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrengthTarget", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureShadowEvBudget", -1.0f) >= 0.0f &&
        payload.integratedToneLayerJson.value("autoAuthoredLocalExposureHighlightEvBudget", -1.0f) >= 0.0f;
    const bool brightHighlightRolloffRenderPayloadConstrained =
        brightHighlightRolloffGenerated &&
        brightHighlightRolloffEligible &&
        brightHighlightRolloffHumanReadable &&
        brightHighlightRolloffDiagnosticsWritten &&
        std::abs(brightHighlightRolloffPayload.settings.exposureStops -
            baseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(brightHighlightRolloffPayload.scenePrepSettings.maxEvBias -
            baseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "brightHighlightRolloff" &&
        brightHighlightRolloffPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            payload.autoGuidance.highlightCharacter + 0.16f;
    const bool luminousHighlightAnchorRenderPayloadConstrained =
        luminousHighlightAnchorGenerated &&
        luminousHighlightAnchorEligible &&
        luminousHighlightAnchorHumanReadable &&
        luminousHighlightAnchorDiagnosticsWritten &&
        std::abs(luminousHighlightAnchorPayload.settings.exposureStops -
            baseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(luminousHighlightAnchorPayload.scenePrepSettings.maxEvBias -
            baseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "luminousHighlightAnchor" &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            payload.autoGuidance.highlightCharacter + 0.22f &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) >
            payload.autoGuidance.contrastBias + 0.10f &&
        luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoDynamicRange", 99.0f) <
            payload.autoGuidance.dynamicRange - 0.04f;
    const bool localRangeGuardRenderPayloadConstrained =
        regionalEvidenceDiagnosticsWritten &&
        localRangeGuardGenerated &&
        localRangeGuardEligible &&
        localRangeGuardDiagnosticsWritten &&
        std::abs(localRangeGuardPayload.settings.exposureStops -
            regionalBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(localRangeGuardPayload.settings.highlightStrength -
            regionalBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        localRangeGuardPayload.scenePrepSettings.maxEvBias >=
            regionalBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.0001f &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true);
    const bool localExposureStrategyCandidatePayloadCarried =
        localExposureStrategyDiagnosticsWritten &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1" &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrategyId", std::string()) ==
            regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyId", std::string()) &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureRangeRedistribution", -1.0f) >= 0.0f &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureHaloGuard", -1.0f) >= 0.0f &&
        localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrengthTarget", -1.0f) >= 0.0f;
    const auto raisedTowardScenePrepGuardCap =
        [](float candidateValue, float baseValue, float intendedDelta) {
            return candidateValue >= std::min(1.0f, baseValue + intendedDelta) - 0.0001f;
        };
    const bool haloSafeLocalRangeRenderPayloadConstrained =
        regionalEvidenceDiagnosticsWritten &&
        haloSafeLocalRangeGenerated &&
        haloSafeLocalRangeEligible &&
        haloSafeLocalRangeHumanReadable &&
        haloSafeLocalRangeDiagnosticsWritten &&
        std::abs(haloSafeLocalRangePayload.settings.exposureStops -
            regionalBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(haloSafeLocalRangePayload.settings.highlightStrength -
            regionalBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        haloSafeLocalRangePayload.scenePrepSettings.maxEvBias <
            regionalBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.04f &&
        raisedTowardScenePrepGuardCap(
            haloSafeLocalRangePayload.scenePrepSettings.haloGuard,
            regionalBaseCandidateRenderPayload.scenePrepSettings.haloGuard,
            0.16f) &&
        raisedTowardScenePrepGuardCap(
            haloSafeLocalRangePayload.scenePrepSettings.smoothGradientProtection,
            regionalBaseCandidateRenderPayload.scenePrepSettings.smoothGradientProtection,
            0.14f) &&
        raisedTowardScenePrepGuardCap(
            haloSafeLocalRangePayload.scenePrepSettings.edgeAwareness,
            regionalBaseCandidateRenderPayload.scenePrepSettings.edgeAwareness,
            0.10f) &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "haloSafeLocalRange";
    const bool shadowNoiseFloorRenderPayloadConstrained =
        regionalEvidenceDiagnosticsWritten &&
        shadowNoiseFloorGenerated &&
        shadowNoiseFloorEligible &&
        shadowNoiseFloorDiagnosticsWritten &&
        std::abs(shadowNoiseFloorPayload.settings.exposureStops -
            regionalBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(shadowNoiseFloorPayload.settings.highlightStrength -
            regionalBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        shadowNoiseFloorPayload.scenePrepSettings.maxEvBias <=
            regionalBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.08f &&
        shadowNoiseFloorPayload.scenePrepSettings.noiseProtectionBias >=
            regionalBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias + 0.08f &&
        shadowNoiseFloorPayload.scenePrepSettings.shadowLiftLimitBias >=
            regionalBaseCandidateRenderPayload.scenePrepSettings.shadowLiftLimitBias + 0.08f &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "shadowNoiseFloor";
    const bool subjectReadableMidsRenderPayloadConstrained =
        subjectReadableMidsGenerated &&
        subjectReadableMidsEligible &&
        subjectReadableMidsHumanReadable &&
        subjectReadableMidsDiagnosticsWritten &&
        std::abs(subjectReadableMidsPayload.settings.exposureStops -
            userSubjectBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(subjectReadableMidsPayload.settings.highlightStrength -
            userSubjectBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        subjectReadableMidsPayload.scenePrepSettings.maxEvBias >=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.0001f &&
        subjectReadableMidsPayload.scenePrepSettings.noiseProtectionBias >=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias + 0.02f &&
        subjectReadableMidsPayload.scenePrepSettings.shadowLiftLimitBias <=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.shadowLiftLimitBias + 0.0001f &&
        subjectReadableMidsPayload.scenePrepSettings.wellExposedTargetBias >=
            userSubjectBaseCandidateRenderPayload.scenePrepSettings.wellExposedTargetBias + 0.02f &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "subjectReadableMids" &&
        subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string()) == "subjectReadableMids";
    const bool sceneMoodPreservationRenderPayloadConstrained =
        sceneMoodPreservationGenerated &&
        sceneMoodPreservationEligible &&
        sceneMoodPreservationHumanReadable &&
        sceneMoodPreservationDiagnosticsWritten &&
        std::abs(sceneMoodPreservationPayload.settings.exposureStops -
            sceneMoodBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(sceneMoodPreservationPayload.settings.highlightStrength -
            sceneMoodBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        sceneMoodPreservationPayload.scenePrepSettings.maxEvBias <=
            sceneMoodBaseCandidateRenderPayload.scenePrepSettings.maxEvBias - 0.06f &&
        sceneMoodPreservationPayload.scenePrepSettings.noiseProtectionBias >=
            sceneMoodBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias + 0.05f &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "sceneMoodPreservation" &&
        sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string()) == "sceneMoodPreservation";
    const bool whiteBalanceProbeRenderPayloadDiverges =
        whiteBalanceProbeGenerated &&
        whiteBalanceProbeEligible &&
        whiteBalanceProbePayload.settings.whiteBalanceMode !=
            baseCandidateRenderPayload.settings.whiteBalanceMode &&
        whiteBalanceProbePayload.settings.manualWhiteBalance ==
            baseCandidateRenderPayload.settings.manualWhiteBalance &&
        whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "rawGlobal" &&
        !whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", true) &&
        whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateWhiteBalanceProbe", std::string()) ==
            whiteBalanceProbeRenderId &&
        whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateWhiteBalanceMode", std::string()) ==
            Raw::WhiteBalanceModeName(whiteBalanceProbePayload.settings.whiteBalanceMode);
    const bool renderedStageRelevanceWorks =
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "renderedLocalBrightenMids",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "maximumRange",
            "rawGlobal") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "modeNeighborNaturalMoreContrast",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "toneSofterRolloff",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "brightHighlightRolloff",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "luminousHighlightAnchor",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "specularHighlightTolerance",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "naturalContrastGuard",
            "finishTone") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "broadHighlightGuard",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "haloSafeLocalRange",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "localRangeGuard",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "shadowReadabilityLift",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "shadowNoiseFloor",
            "scenePrep") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "renderedLocalCleanShadows",
            "rawCleanup") &&
        EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "wbDaylightCorrection",
            "rawGlobal") &&
        !EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
            "renderedLocalBrightenMids",
            "none");
    const bool renderedRefineIntentRelevanceWorks =
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalHighlightRestraint",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "highlightProtectedMids",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "brightHighlightRolloff",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "luminousHighlightAnchor",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "specularHighlightTolerance",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "broadHighlightGuard",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "localRangeGuard",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "haloSafeLocalRange",
            "protectHighlights") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "haloSafeLocalRange",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "localRangeGuard",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "shadowReadabilityLift",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "shadowNoiseFloor",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "shadowNoiseFloor",
            "cleanShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalCleanShadows",
            "cleanShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "tonePunchierShape",
            "addContrast") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "naturalContrastGuard",
            "addContrast") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "luminousHighlightAnchor",
            "addContrast") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalShadowOpening",
            "openShadows") &&
        EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalContrastShape",
            "addContrast") &&
        !EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalContrastShape",
            "cleanShadows") &&
        !EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
            "renderedLocalBrightenMids",
            "");
    bool finishToneStageCacheMet = false;
    std::string finishToneStageCacheExpected;
    std::string finishToneStageCacheStatus;
    const std::string finishToneObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "finishTone",
            true,
            true,
            finishToneStageCacheMet,
            finishToneStageCacheExpected,
            finishToneStageCacheStatus);
    bool scenePrepStageCacheMet = false;
    std::string scenePrepStageCacheExpected;
    std::string scenePrepStageCacheStatus;
    const std::string scenePrepObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "scenePrep",
            true,
            false,
            scenePrepStageCacheMet,
            scenePrepStageCacheExpected,
            scenePrepStageCacheStatus);
    bool scenePrepStageCacheMissMet = true;
    std::string scenePrepStageCacheMissExpected;
    std::string scenePrepStageCacheMissStatus;
    const std::string scenePrepMissObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "scenePrep",
            false,
            false,
            scenePrepStageCacheMissMet,
            scenePrepStageCacheMissExpected,
            scenePrepStageCacheMissStatus);
    bool rawStageCacheMet = false;
    std::string rawStageCacheExpected;
    std::string rawStageCacheStatus;
    const std::string rawStageObservedDirtyBoundary =
        EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
            "rawGlobal",
            false,
            false,
            rawStageCacheMet,
            rawStageCacheExpected,
            rawStageCacheStatus);
    const bool renderedStageCacheValidationWorks =
        finishToneObservedDirtyBoundary == "finishTone" &&
        finishToneStageCacheMet &&
        finishToneStageCacheExpected == "finishTone" &&
        finishToneStageCacheStatus == "met" &&
        scenePrepObservedDirtyBoundary == "scenePrep" &&
        scenePrepStageCacheMet &&
        scenePrepStageCacheExpected == "scenePrep" &&
        scenePrepStageCacheStatus == "met" &&
        scenePrepMissObservedDirtyBoundary == "rawBase" &&
        !scenePrepStageCacheMissMet &&
        scenePrepStageCacheMissStatus == "missedRawBaseReuse" &&
        rawStageObservedDirtyBoundary == "rawBase" &&
        rawStageCacheMet &&
        rawStageCacheExpected == "rawBase" &&
        rawStageCacheStatus == "notRequired";
    std::string selectedScheduleBoundary;
    std::string selectedScheduleReason;
    const int selectedScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "multiStage",
            true,
            selectedScheduleBoundary,
            selectedScheduleReason);
    std::string finishToneScheduleBoundary;
    std::string finishToneScheduleReason;
    const int finishToneScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "finishTone",
            false,
            finishToneScheduleBoundary,
            finishToneScheduleReason);
    std::string scenePrepScheduleBoundary;
    std::string scenePrepScheduleReason;
    const int scenePrepScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "scenePrep",
            false,
            scenePrepScheduleBoundary,
            scenePrepScheduleReason);
    std::string rawGlobalScheduleBoundary;
    std::string rawGlobalScheduleReason;
    const int rawGlobalScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "rawGlobal",
            false,
            rawGlobalScheduleBoundary,
            rawGlobalScheduleReason);
    std::string multiStageScheduleBoundary;
    std::string multiStageScheduleReason;
    const int multiStageScheduleRank =
        EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
            "multiStage",
            false,
            multiStageScheduleBoundary,
            multiStageScheduleReason);
    const bool renderedStageSchedulerClassificationWorks =
        selectedScheduleRank == 0 &&
        finishToneScheduleRank > selectedScheduleRank &&
        finishToneScheduleRank < scenePrepScheduleRank &&
        scenePrepScheduleRank < rawGlobalScheduleRank &&
        rawGlobalScheduleRank < multiStageScheduleRank &&
        selectedScheduleBoundary == "rawBase" &&
        finishToneScheduleBoundary == "finishTone" &&
        scenePrepScheduleBoundary == "scenePrep" &&
        rawGlobalScheduleBoundary == "rawBase" &&
        multiStageScheduleBoundary == "rawBase" &&
        finishToneScheduleReason.find("pre-finish caches") != std::string::npos &&
        rawGlobalScheduleReason.find("after downstream") != std::string::npos;
    const bool developCandidateRenderBudgetAllowsMultiNodeCoverage =
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(0, 0) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(3, 3) &&
        !EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(4, 4) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(4, 0) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(19, 0) &&
        !EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(20, 0) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(4, 4, 6) &&
        EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(5, 5, 6) &&
        !EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(6, 6, 6);
    const bool developCandidateMetricReadbackBudgetWorks =
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(4000, 3000) == 0 &&
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(5000, 4000) == 1800 &&
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(7000, 5000) == 1536 &&
        EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(9000, 6000) == 1280;
    const bool rawDevelopStageCacheMemoryPolicyWorks =
        RenderPipeline::EstimateRawDevelopStageCacheTextureBytesForValidation(1000, 1000) == 8000000ull &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(2000, 1500) == 6 &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(4000, 3000) == 3 &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(5000, 4000) == 2 &&
        RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(7000, 5000) == 1 &&
        !RenderPipeline::ShouldCacheRawDevelopStageTextureForValidation(9000, 6000);
    nlohmann::json adaptiveContinueTone = nlohmann::json::object();
    adaptiveContinueTone["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "continue" },
        { "reason", "merged" },
        { "nextStep", "renderUpdatedSolve" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "pass", 1 },
        { "remainingPasses", 2 }
    };
    std::string adaptiveContinueReason;
    bool adaptiveContinueExpanded = false;
    const std::size_t adaptiveContinueBudget =
        EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
            adaptiveContinueTone,
            42,
            41,
            7,
            "finishTone",
            "protectHighlights",
            adaptiveContinueReason,
            adaptiveContinueExpanded);
    nlohmann::json adaptiveInitialTone = nlohmann::json::object();
    adaptiveInitialTone["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "nextStep", "renderCandidates" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "pass", 0 },
        { "remainingPasses", 3 }
    };
    std::string adaptiveInitialReason;
    bool adaptiveInitialExpanded = true;
    const std::size_t adaptiveInitialBudget =
        EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
            adaptiveInitialTone,
            42,
            41,
            7,
            "",
            "",
            adaptiveInitialReason,
            adaptiveInitialExpanded);
    nlohmann::json adaptiveFocusedTone = nlohmann::json::object();
    adaptiveFocusedTone["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "nextStep", "renderCandidates" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "pass", 2 },
        { "remainingPasses", 1 }
    };
    adaptiveFocusedTone["autoCandidateConvergenceEvidence"] = {
        { "version", "ConvergenceEvidenceV1" },
        { "state", "awaitingRenderedMetrics" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "pass", 2 },
        { "shouldContinue", true }
    };
    adaptiveFocusedTone["autoCandidateConvergenceAdmissionTightened"] = true;
    std::string adaptiveFocusedReason;
    bool adaptiveFocusedExpanded = true;
    bool adaptiveFocusedNarrowed = false;
    const std::size_t adaptiveFocusedBudget =
        EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
            adaptiveFocusedTone,
            42,
            41,
            7,
            "",
            "",
            adaptiveFocusedReason,
            adaptiveFocusedExpanded,
            &adaptiveFocusedNarrowed);
    const bool developAdaptiveRenderBudgetWorks =
        adaptiveContinueBudget == 6 &&
        adaptiveContinueExpanded &&
        adaptiveContinueReason == "validateActiveRefineIntent" &&
        adaptiveInitialBudget == 4 &&
        !adaptiveInitialExpanded &&
        adaptiveInitialReason == "initialRenderedMetrics" &&
        adaptiveFocusedBudget == 3 &&
        !adaptiveFocusedExpanded &&
        adaptiveFocusedNarrowed &&
        adaptiveFocusedReason == "convergenceEvidenceFocusedValidation";
    const double candidateFeedbackQuietSeconds =
        EditorModule::DevelopCandidateFeedbackQuietSecondsForValidation();
    const bool developCandidateFeedbackGateDropsStale =
        EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
            7,
            8,
            10.0,
            11.0) ==
        EditorModule::DevelopCandidateFeedbackGateDecision::DropStaleInteraction;
    const bool developCandidateFeedbackGateDefersRecent =
        candidateFeedbackQuietSeconds >= 0.59 &&
        EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
            8,
            8,
            10.0,
            10.0 + candidateFeedbackQuietSeconds * 0.50) ==
        EditorModule::DevelopCandidateFeedbackGateDecision::DeferRecentInteraction;
    const bool developCandidateFeedbackGateAppliesAfterQuiet =
        EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
            8,
            8,
            10.0,
            10.0 + candidateFeedbackQuietSeconds + 0.01) ==
        EditorModule::DevelopCandidateFeedbackGateDecision::Apply;
    const bool developCandidateRenderAdmissionDefersRecent =
        EditorModule::ShouldDeferDevelopCandidateRenderRequestForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds * 0.50) &&
        !EditorModule::ShouldDeferDevelopCandidateRenderRequestForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds + 0.01);
    const double candidateFeedbackRemainingMidEdit =
        EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds * 0.25);
    const bool developCandidateFeedbackQuietRemainingWorks =
        std::abs(candidateFeedbackRemainingMidEdit - candidateFeedbackQuietSeconds * 0.75) < 0.000001 &&
        EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
            10.0,
            10.0 + candidateFeedbackQuietSeconds + 0.01) == 0.0 &&
        EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
            10.0,
            9.99) == 0.0;
    const bool developStaleSnapshotAbortWorks =
        !EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, false, false, 0) &&
        !EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, false, true, 12) &&
        EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, false, true, 13) &&
        EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(12, true, false, 0);
    const std::string candidateProgressLabel =
        EditorRenderWorker::BuildDevelopCandidateProgressLabelForValidation(
            "Luminous Highlight Anchor Candidate With A Very Long User-Facing Name",
            "finishTone",
            1,
            4);
    const bool developCandidateProgressLabelWorks =
        candidateProgressLabel.find("2/4") != std::string::npos &&
        candidateProgressLabel.find("Luminous Highlight Anchor") != std::string::npos &&
        candidateProgressLabel.find("finishTone") != std::string::npos &&
        candidateProgressLabel.size() < 110;
    const bool finishGuidanceForwarded =
        requestedGuidanceForwarded &&
        selectedCandidateGuidanceForwarded &&
        candidateDiagnosticsWritten;
    const bool brightnessExposureTelemetryForwarded =
        requestedGuidanceForwarded &&
        selectedCandidateGuidanceForwarded &&
        candidateSolveCanBiasAuthoredGuidance &&
        std::abs(payload.integratedToneLayerJson.value("autoRawExposurePreferenceEv", -99.0f) -
            payload.integratedToneLayerJson.value("autoBrightnessIntent", -98.0f) * 2.0f) < 0.0001f;
    const bool exposureDiagnosticsForwarded =
        payload.integratedToneLayerJson.value("autoExposureDiagnosticStatsValid", false) &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredRawExposureEv", -99.0f) - payload.settings.exposureStops) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredRawExposureScale", -99.0f) - std::exp2(payload.settings.exposureStops)) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredLocalMinEvBias", -99.0f) - payload.scenePrepSettings.minEvBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoAuthoredLocalMaxEvBias", -99.0f) - payload.scenePrepSettings.maxEvBias) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticClippingRatio", -99.0f) - 0.0f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticHighlightPressure", -99.0f) - 0.68f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticNoiseRisk", -99.0f) - 0.57f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticHdrSpreadEv", -99.0f) - 4.6f) < 0.0001f &&
        std::abs(payload.integratedToneLayerJson.value("autoExposureDiagnosticRecommendedBaseEv", -99.0f) - 1.35f) < 0.0001f;

    constexpr int visualRiskW = 6;
    constexpr int visualRiskH = 6;
    std::vector<unsigned char> visualRiskPixels(static_cast<size_t>(visualRiskW * visualRiskH * 4), 255);
    auto setVisualRiskPixel = [&](int x, int y, unsigned char value) {
        const size_t offset = static_cast<size_t>((y * visualRiskW + x) * 4);
        visualRiskPixels[offset + 0] = value;
        visualRiskPixels[offset + 1] = value;
        visualRiskPixels[offset + 2] = value;
        visualRiskPixels[offset + 3] = 255;
    };
    for (int y = 0; y < visualRiskH; ++y) {
        for (int x = 0; x < visualRiskW; ++x) {
            const unsigned char value = x < 3
                ? static_cast<unsigned char>(((x + y) & 1) ? 55 : 18)
                : static_cast<unsigned char>(((x + y) & 1) ? 224 : 184);
            setVisualRiskPixel(x, y, value);
        }
    }
    setVisualRiskPixel(2, 2, 255);
    const EditorRenderWorker::DevelopCandidateRenderMetrics visualRiskMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            visualRiskPixels,
            visualRiskW,
            visualRiskH);
    std::vector<unsigned char> spatialRiskPixels(static_cast<size_t>(visualRiskW * visualRiskH * 4), 96);
    for (size_t offset = 0; offset < spatialRiskPixels.size(); offset += 4u) {
        spatialRiskPixels[offset + 0] = 96;
        spatialRiskPixels[offset + 1] = 96;
        spatialRiskPixels[offset + 2] = 96;
        spatialRiskPixels[offset + 3] = 255;
    }
    auto setSpatialRiskPixel = [&](int x, int y, unsigned char value) {
        const size_t offset = static_cast<size_t>((y * visualRiskW + x) * 4);
        spatialRiskPixels[offset + 0] = value;
        spatialRiskPixels[offset + 1] = value;
        spatialRiskPixels[offset + 2] = value;
        spatialRiskPixels[offset + 3] = 255;
    };
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            setSpatialRiskPixel(x, y, 245);
        }
    }
    const EditorRenderWorker::DevelopCandidateRenderMetrics spatialRiskMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            spatialRiskPixels,
            visualRiskW,
            visualRiskH);
    const bool renderedVisualRiskMetricsPopulated =
        visualRiskMetrics.contrastSpan > 0.45f &&
        visualRiskMetrics.lowSaturationFraction > 0.45f &&
        visualRiskMetrics.edgeContrast > 0.20f &&
        visualRiskMetrics.haloRiskFraction > 0.0f &&
        visualRiskMetrics.shadowTextureRisk > 0.05f &&
        visualRiskMetrics.localExposureHighlightCrowding > 0.10f &&
        visualRiskMetrics.localExposureShadowCrowding > 0.10f &&
        visualRiskMetrics.localExposureHaloStress > 0.05f &&
        visualRiskMetrics.localExposureDamageRisk > 0.05f;
    const bool renderedHighlightGrayMetricsPopulated =
        visualRiskMetrics.highlightBandFraction > 0.25f &&
        visualRiskMetrics.highlightMeanLuma > 0.55f &&
        visualRiskMetrics.highlightLowSaturationFraction > 0.70f &&
        visualRiskMetrics.highlightGrayRisk > 0.10f;
    const bool renderedMeaningfulHighlightMetricsPopulated =
        visualRiskMetrics.highlightTileCoverage > 0.25f &&
        visualRiskMetrics.highlightStructureScore > 0.10f &&
        visualRiskMetrics.meaningfulHighlightPressure > 0.22f;
    const bool renderedLocalMetricsPopulated =
        visualRiskMetrics.localLumaSpread > 0.35f &&
        visualRiskMetrics.localEvSpreadStops > 1.0f &&
        visualRiskMetrics.localEvConflict > 0.20f &&
        visualRiskMetrics.localContrastPeak > 0.40f &&
        visualRiskMetrics.localShadowPressure > 0.30f &&
        visualRiskMetrics.centerMeanLuma > 0.10f;
    std::vector<unsigned char> subjectRiskPixels(static_cast<size_t>(visualRiskW * visualRiskH * 4), 128);
    for (size_t offset = 0; offset < subjectRiskPixels.size(); offset += 4u) {
        subjectRiskPixels[offset + 0] = 122;
        subjectRiskPixels[offset + 1] = 122;
        subjectRiskPixels[offset + 2] = 122;
        subjectRiskPixels[offset + 3] = 255;
    }
    auto setSubjectRiskPixel = [&](int x, int y, unsigned char value) {
        const size_t offset = static_cast<size_t>((y * visualRiskW + x) * 4);
        subjectRiskPixels[offset + 0] = value;
        subjectRiskPixels[offset + 1] = value;
        subjectRiskPixels[offset + 2] = value;
        subjectRiskPixels[offset + 3] = 255;
    };
    for (int y = 0; y < visualRiskH; ++y) {
        for (int x = 4; x < visualRiskW; ++x) {
            setSubjectRiskPixel(x, y, 208);
        }
    }
    setSubjectRiskPixel(2, 2, 34);
    setSubjectRiskPixel(3, 2, 78);
    setSubjectRiskPixel(2, 3, 46);
    setSubjectRiskPixel(3, 3, 92);
    const EditorRenderWorker::DevelopCandidateRenderMetrics subjectRiskMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            subjectRiskPixels,
            visualRiskW,
            visualRiskH);
    const bool renderedSubjectMetricsPopulated =
        subjectRiskMetrics.subjectCenterPrior > 0.45f &&
        subjectRiskMetrics.subjectImportanceConfidence > 0.35f &&
        std::max({
            subjectRiskMetrics.subjectReadabilityPressure,
            subjectRiskMetrics.subjectProtectionPressure,
            subjectRiskMetrics.subjectMoodPreservationPressure }) > 0.10f;
    EditorRenderWorker::DevelopSubjectMetricSampling markedSubjectSampling;
    markedSubjectSampling.enabled = true;
    EditorRenderWorker::DevelopSubjectMetricRegion markedSubjectRegion;
    markedSubjectRegion.mode =
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Reveal);
    markedSubjectRegion.centerX = 0.50f;
    markedSubjectRegion.centerY = 0.50f;
    markedSubjectRegion.radiusX = 0.30f;
    markedSubjectRegion.radiusY = 0.30f;
    markedSubjectRegion.feather = 0.20f;
    markedSubjectRegion.strength = 0.95f;
    markedSubjectSampling.regions.push_back(markedSubjectRegion);
    const EditorRenderWorker::DevelopCandidateRenderMetrics markedSubjectMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            subjectRiskPixels,
            visualRiskW,
            visualRiskH,
            markedSubjectSampling);
    EditorRenderWorker::DevelopSubjectMetricSampling lowPrioritySubjectSampling;
    lowPrioritySubjectSampling.enabled = true;
    EditorRenderWorker::DevelopSubjectMetricRegion lowPrioritySubjectRegion;
    lowPrioritySubjectRegion.mode =
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore);
    lowPrioritySubjectRegion.lowPriority = true;
    lowPrioritySubjectRegion.centerX = 0.84f;
    lowPrioritySubjectRegion.centerY = 0.50f;
    lowPrioritySubjectRegion.radiusX = 0.24f;
    lowPrioritySubjectRegion.radiusY = 0.55f;
    lowPrioritySubjectRegion.feather = 0.15f;
    lowPrioritySubjectRegion.strength = 0.90f;
    lowPrioritySubjectSampling.regions.push_back(lowPrioritySubjectRegion);
    const EditorRenderWorker::DevelopCandidateRenderMetrics lowPrioritySubjectMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            subjectRiskPixels,
            visualRiskW,
            visualRiskH,
            lowPrioritySubjectSampling);
    const bool renderedMarkedSubjectMetricsPopulated =
        markedSubjectMetrics.subjectMarkedSampleCount > 0 &&
        markedSubjectMetrics.subjectMarkedCoverage > 0.04f &&
        markedSubjectMetrics.subjectMarkedPositiveCoverage > 0.04f &&
        markedSubjectMetrics.subjectMarkedRevealCoverage > 0.01f &&
        markedSubjectMetrics.subjectMarkedMeanLuma > 0.10f &&
        markedSubjectMetrics.subjectMarkedMeanLuma < 0.55f &&
        markedSubjectMetrics.subjectMarkedReadabilityScore > 0.20f &&
        subjectRiskMetrics.subjectMarkedCoverage == 0.0f;
    const bool renderedMarkedLowPriorityMetricsPopulated =
        lowPrioritySubjectMetrics.subjectMarkedLowPriorityCoverage > 0.04f &&
        lowPrioritySubjectMetrics.subjectMarkedLowPriorityMeanLuma > 0.60f &&
        lowPrioritySubjectMetrics.subjectMarkedLowPriorityBrightFraction > 0.70f &&
        lowPrioritySubjectMetrics.subjectMarkedLowPriorityPressure > 0.03f;
    const bool renderedSpatialRiskMetricsPopulated =
        spatialRiskMetrics.localDamageRiskPeak > 0.10f &&
        spatialRiskMetrics.localDamageRiskMean > 0.03f &&
        spatialRiskMetrics.localDamageRiskPeakTile >= 0 &&
        spatialRiskMetrics.localDamageRiskPeakTile < 9 &&
        spatialRiskMetrics.localDamageRiskScore[static_cast<size_t>(spatialRiskMetrics.localDamageRiskPeakTile)] >=
            spatialRiskMetrics.localDamageRiskPeak - 0.0001f;
    constexpr int colorCastW = 4;
    constexpr int colorCastH = 4;
    std::vector<unsigned char> colorCastPixels(static_cast<size_t>(colorCastW * colorCastH * 4), 255);
    for (size_t offset = 0; offset < colorCastPixels.size(); offset += 4u) {
        colorCastPixels[offset + 0] = 24;
        colorCastPixels[offset + 1] = 212;
        colorCastPixels[offset + 2] = 42;
        colorCastPixels[offset + 3] = 255;
    }
    const EditorRenderWorker::DevelopCandidateRenderMetrics colorCastMetrics =
        EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
            colorCastPixels,
            colorCastW,
            colorCastH);
    const bool renderedColorCastMetricsPopulated =
        colorCastMetrics.meanGreen > colorCastMetrics.meanRed + 0.50f &&
        colorCastMetrics.meanGreen > colorCastMetrics.meanBlue + 0.50f &&
        colorCastMetrics.magentaGreenBias < -0.20f &&
        colorCastMetrics.channelImbalance > 0.75f &&
        colorCastMetrics.colorCastRisk > 0.75f;
    EditorRenderWorker::DevelopCandidateRenderMetrics similarRenderedMetrics = visualRiskMetrics;
    similarRenderedMetrics.meanLuma += 0.005f;
    similarRenderedMetrics.medianLuma += 0.004f;
    EditorRenderWorker::DevelopCandidateRenderMetrics distinctRenderedMetrics = visualRiskMetrics;
    distinctRenderedMetrics.meanLuma = 0.72f;
    distinctRenderedMetrics.medianLuma = 0.74f;
    distinctRenderedMetrics.p10Luma = 0.58f;
    distinctRenderedMetrics.p90Luma = 0.96f;
    distinctRenderedMetrics.shadowFraction = 0.02f;
    distinctRenderedMetrics.highlightFraction = 0.44f;
    distinctRenderedMetrics.clippedFraction = 0.08f;
    const float renderedDuplicateMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(visualRiskMetrics, similarRenderedMetrics);
    const float renderedDistinctMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(visualRiskMetrics, distinctRenderedMetrics);
    EditorRenderWorker::DevelopCandidateRenderMetrics localOnlyDistinctMetrics = visualRiskMetrics;
    for (float& tileMean : localOnlyDistinctMetrics.localMeanLuma) {
        tileMean = 1.0f - tileMean;
    }
    localOnlyDistinctMetrics.localLumaSpread = 1.0f;
    localOnlyDistinctMetrics.centerMeanLuma = 1.0f - visualRiskMetrics.centerMeanLuma;
    const float renderedLocalMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(visualRiskMetrics, localOnlyDistinctMetrics);
    EditorRenderWorker::DevelopCandidateRenderMetrics neutralColorMetrics = visualRiskMetrics;
    neutralColorMetrics.meanRed = 0.42f;
    neutralColorMetrics.meanGreen = 0.42f;
    neutralColorMetrics.meanBlue = 0.42f;
    neutralColorMetrics.warmCoolBias = 0.0f;
    neutralColorMetrics.magentaGreenBias = 0.0f;
    neutralColorMetrics.channelImbalance = 0.0f;
    neutralColorMetrics.colorCastRisk = 0.0f;
    EditorRenderWorker::DevelopCandidateRenderMetrics distinctColorMetrics = neutralColorMetrics;
    distinctColorMetrics.meanRed = 0.12f;
    distinctColorMetrics.meanGreen = 0.72f;
    distinctColorMetrics.meanBlue = 0.14f;
    distinctColorMetrics.magentaGreenBias = -0.44f;
    distinctColorMetrics.channelImbalance = 0.83f;
    distinctColorMetrics.colorCastRisk = 0.92f;
    const float renderedColorMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(neutralColorMetrics, distinctColorMetrics);
    EditorRenderWorker::DevelopCandidateRenderMetrics distinctMarkedSubjectMetrics = markedSubjectMetrics;
    distinctMarkedSubjectMetrics.subjectMarkedMeanLuma = 0.90f;
    distinctMarkedSubjectMetrics.subjectMarkedReadabilityScore = 0.04f;
    distinctMarkedSubjectMetrics.subjectMarkedProtectionRisk = 0.88f;
    const float renderedMarkedSubjectMetricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(markedSubjectMetrics, distinctMarkedSubjectMetrics);
    const bool renderedDuplicateMetricDistanceWorks =
        renderedDuplicateMetricDistance < 0.085f &&
        renderedDistinctMetricDistance > 0.085f &&
        renderedLocalMetricDistance > 0.085f &&
        renderedColorMetricDistance > 0.085f &&
        renderedMarkedSubjectMetricDistance > 0.085f;
    float finishOnlyFinalMetricDistance = -1.0f;
    float finishOnlyPreFinishMetricDistance = -1.0f;
    const std::string finishOnlyStageBoundary =
        EditorModule::ClassifyDevelopRenderedStageBoundaryForValidation(
            visualRiskMetrics,
            distinctRenderedMetrics,
            true,
            visualRiskMetrics,
            similarRenderedMetrics,
            true,
            finishOnlyFinalMetricDistance,
            finishOnlyPreFinishMetricDistance);
    float preFinishChangedFinalMetricDistance = -1.0f;
    float preFinishChangedPreFinishMetricDistance = -1.0f;
    const std::string preFinishChangedStageBoundary =
        EditorModule::ClassifyDevelopRenderedStageBoundaryForValidation(
            visualRiskMetrics,
            similarRenderedMetrics,
            true,
            visualRiskMetrics,
            distinctRenderedMetrics,
            true,
            preFinishChangedFinalMetricDistance,
            preFinishChangedPreFinishMetricDistance);
    const bool renderedStageBoundaryClassifierWorks =
        finishOnlyStageBoundary == "finishToneOnly" &&
        finishOnlyFinalMetricDistance >= 0.085f &&
        finishOnlyPreFinishMetricDistance <= 0.055f &&
        preFinishChangedStageBoundary == "preFinishChangedButFinalMasked" &&
        preFinishChangedFinalMetricDistance < 0.085f &&
        preFinishChangedPreFinishMetricDistance >= 0.085f;
    float stageAwareDuplicateFinalDistance = -1.0f;
    float stageAwareDuplicatePreFinishDistance = -1.0f;
    bool stageAwareDuplicatePreFinishDistinct = false;
    const bool stageAwareDuplicateFinalAndPreFinishClose =
        EditorModule::ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
            similarRenderedMetrics,
            visualRiskMetrics,
            true,
            similarRenderedMetrics,
            true,
            visualRiskMetrics,
            stageAwareDuplicateFinalDistance,
            stageAwareDuplicatePreFinishDistance,
            stageAwareDuplicatePreFinishDistinct);
    float stageAwareMaskedFinalDistance = -1.0f;
    float stageAwareMaskedPreFinishDistance = -1.0f;
    bool stageAwareMaskedPreFinishDistinct = false;
    const bool stageAwareDuplicatePreservesPreFinishDistinct =
        !EditorModule::ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
            similarRenderedMetrics,
            visualRiskMetrics,
            true,
            distinctRenderedMetrics,
            true,
            visualRiskMetrics,
            stageAwareMaskedFinalDistance,
            stageAwareMaskedPreFinishDistance,
            stageAwareMaskedPreFinishDistinct);
    const bool renderedStageAwareDuplicateClusteringWorks =
        stageAwareDuplicateFinalAndPreFinishClose &&
        !stageAwareDuplicatePreFinishDistinct &&
        stageAwareDuplicateFinalDistance < 0.085f &&
        stageAwareDuplicatePreFinishDistance < 0.085f &&
        stageAwareDuplicatePreservesPreFinishDistinct &&
        stageAwareMaskedPreFinishDistinct &&
        stageAwareMaskedFinalDistance < 0.085f &&
        stageAwareMaskedPreFinishDistance >= 0.085f;
    EditorRenderWorker::DevelopCandidateRenderMetrics damagedClipMetrics = distinctRenderedMetrics;
    damagedClipMetrics.clippedFraction = 0.09f;
    damagedClipMetrics.highlightFraction = 0.82f;
    EditorRenderWorker::DevelopCandidateRenderMetrics damagedHaloMetrics = visualRiskMetrics;
    damagedHaloMetrics.haloRiskFraction = 0.26f;
    damagedHaloMetrics.edgeContrast = 0.48f;
    damagedHaloMetrics.localContrastPeak = 0.82f;
    EditorRenderWorker::DevelopCandidateRenderMetrics damagedGrayMetrics = visualRiskMetrics;
    damagedGrayMetrics.lowSaturationFraction = 0.96f;
    damagedGrayMetrics.meanSaturation = 0.03f;
    damagedGrayMetrics.contrastSpan = 0.18f;
    EditorRenderWorker::DevelopCandidateRenderMetrics damagedShadowNoiseMetrics = visualRiskMetrics;
    damagedShadowNoiseMetrics.shadowTextureRisk = 0.94f;
    damagedShadowNoiseMetrics.shadowFraction = 0.58f;
    damagedShadowNoiseMetrics.medianLuma = 0.24f;
    EditorRenderWorker::DevelopCandidateRenderMetrics damagedSpatialHotspotMetrics = visualRiskMetrics;
    damagedSpatialHotspotMetrics.localDamageRiskScore = {
        0.08f, 0.16f, 0.94f,
        0.12f, 0.18f, 0.88f,
        0.10f, 0.14f, 0.82f
    };
    damagedSpatialHotspotMetrics.localDamageRiskPeak = 0.94f;
    damagedSpatialHotspotMetrics.localDamageRiskMean = 0.36f;
    damagedSpatialHotspotMetrics.localDamageRiskPeakTile = 2;
    damagedSpatialHotspotMetrics.localHighlightPressure = 0.72f;
    damagedSpatialHotspotMetrics.localContrastPeak = 0.90f;
    EditorRenderWorker::DevelopCandidateRenderMetrics safeRenderedMetrics = visualRiskMetrics;
    safeRenderedMetrics.clippedFraction = 0.0f;
    safeRenderedMetrics.highlightFraction = 0.12f;
    safeRenderedMetrics.haloRiskFraction = 0.01f;
    safeRenderedMetrics.edgeContrast = 0.24f;
    safeRenderedMetrics.localContrastPeak = 0.32f;
    safeRenderedMetrics.lowSaturationFraction = 0.36f;
    safeRenderedMetrics.meanSaturation = 0.18f;
    safeRenderedMetrics.contrastSpan = 0.42f;
    safeRenderedMetrics.shadowTextureRisk = 0.16f;
    safeRenderedMetrics.shadowFraction = 0.20f;
    safeRenderedMetrics.medianLuma = 0.38f;
    EditorRenderWorker::DevelopCandidateRenderMetrics damagedColorCastMetrics = safeRenderedMetrics;
    damagedColorCastMetrics.meanRed = 0.12f;
    damagedColorCastMetrics.meanGreen = 0.76f;
    damagedColorCastMetrics.meanBlue = 0.14f;
    damagedColorCastMetrics.magentaGreenBias = -0.46f;
    damagedColorCastMetrics.channelImbalance = 0.84f;
    damagedColorCastMetrics.colorCastRisk = 0.94f;
    damagedColorCastMetrics.meanSaturation = 0.64f;
    const std::string damagedClipReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedClipMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const std::string damagedHaloReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedHaloMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const std::string damagedGrayReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedGrayMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const std::string damagedShadowNoiseReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedShadowNoiseMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const std::string damagedSpatialHotspotReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedSpatialHotspotMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const std::string damagedColorCastReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            damagedColorCastMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const std::string safeRenderedDamageReason =
        EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
            safeRenderedMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished);
    const bool renderedDamageClassifierWorks =
        !damagedClipReason.empty() &&
        !damagedHaloReason.empty() &&
        !damagedGrayReason.empty() &&
        !damagedShadowNoiseReason.empty() &&
        !damagedSpatialHotspotReason.empty() &&
        !damagedColorCastReason.empty() &&
        safeRenderedDamageReason.empty();
    EditorRenderWorker::DevelopCandidateRenderMetrics localCenterShadowMetrics = visualRiskMetrics;
    localCenterShadowMetrics.meanLuma = 0.32f;
    localCenterShadowMetrics.medianLuma = 0.30f;
    localCenterShadowMetrics.shadowFraction = 0.22f;
    localCenterShadowMetrics.highlightFraction = 0.04f;
    localCenterShadowMetrics.clippedFraction = 0.0f;
    localCenterShadowMetrics.centerMeanLuma = 0.11f;
    localCenterShadowMetrics.centerShadowFraction = 0.68f;
    localCenterShadowMetrics.localShadowPressure = 0.68f;
    localCenterShadowMetrics.localHighlightPressure = 0.06f;
    localCenterShadowMetrics.shadowTextureRisk = 0.18f;
    std::string localCenterShadowReason;
    const std::string localCenterShadowIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localCenterShadowMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localCenterShadowReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics localHighlightMetrics = visualRiskMetrics;
    localHighlightMetrics.meanLuma = 0.38f;
    localHighlightMetrics.medianLuma = 0.36f;
    localHighlightMetrics.highlightFraction = 0.12f;
    localHighlightMetrics.clippedFraction = 0.0f;
    localHighlightMetrics.localHighlightPressure = 0.76f;
    localHighlightMetrics.centerHighlightFraction = 0.18f;
    std::string localHighlightReason;
    const std::string localHighlightIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localHighlightMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localHighlightReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics structuredHighlightPressureMetrics = visualRiskMetrics;
    structuredHighlightPressureMetrics.meanLuma = 0.40f;
    structuredHighlightPressureMetrics.medianLuma = 0.36f;
    structuredHighlightPressureMetrics.highlightFraction = 0.12f;
    structuredHighlightPressureMetrics.clippedFraction = 0.0f;
    structuredHighlightPressureMetrics.localHighlightPressure = 0.62f;
    structuredHighlightPressureMetrics.meaningfulHighlightPressure = 0.72f;
    std::string structuredHighlightPressureReason;
    const std::string structuredHighlightPressureIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            structuredHighlightPressureMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            structuredHighlightPressureReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics localSpatialHighlightRiskMetrics = visualRiskMetrics;
    localSpatialHighlightRiskMetrics.meanLuma = 0.40f;
    localSpatialHighlightRiskMetrics.medianLuma = 0.36f;
    localSpatialHighlightRiskMetrics.highlightFraction = 0.12f;
    localSpatialHighlightRiskMetrics.clippedFraction = 0.0f;
    localSpatialHighlightRiskMetrics.haloRiskFraction = 0.0f;
    localSpatialHighlightRiskMetrics.edgeContrast = 0.24f;
    localSpatialHighlightRiskMetrics.localHighlightPressure = 0.62f;
    localSpatialHighlightRiskMetrics.localShadowPressure = 0.12f;
    localSpatialHighlightRiskMetrics.localContrastPeak = 0.48f;
    localSpatialHighlightRiskMetrics.localDamageRiskPeak = 0.78f;
    localSpatialHighlightRiskMetrics.localDamageRiskMean = 0.18f;
    localSpatialHighlightRiskMetrics.centerHighlightFraction = 0.12f;
    localSpatialHighlightRiskMetrics.highlightTileCoverage = 0.0f;
    localSpatialHighlightRiskMetrics.highlightStructureScore = 0.0f;
    localSpatialHighlightRiskMetrics.meaningfulHighlightPressure = 0.0f;
    std::string localSpatialHighlightRiskReason;
    const std::string localSpatialHighlightRiskIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localSpatialHighlightRiskMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localSpatialHighlightRiskReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics localFlatMetrics = visualRiskMetrics;
    localFlatMetrics.meanLuma = 0.36f;
    localFlatMetrics.medianLuma = 0.34f;
    localFlatMetrics.contrastSpan = 0.28f;
    localFlatMetrics.highlightFraction = 0.06f;
    localFlatMetrics.clippedFraction = 0.0f;
    localFlatMetrics.lowSaturationFraction = 0.30f;
    localFlatMetrics.localLumaSpread = 0.08f;
    localFlatMetrics.localContrastPeak = 0.20f;
    localFlatMetrics.localHighlightPressure = 0.04f;
    localFlatMetrics.localShadowPressure = 0.12f;
    std::string localFlatReason;
    const std::string localFlatIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localFlatMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localFlatReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics renderedCleanShadowMetrics = visualRiskMetrics;
    renderedCleanShadowMetrics.meanLuma = 0.31f;
    renderedCleanShadowMetrics.medianLuma = 0.29f;
    renderedCleanShadowMetrics.shadowFraction = 0.34f;
    renderedCleanShadowMetrics.highlightFraction = 0.04f;
    renderedCleanShadowMetrics.clippedFraction = 0.0f;
    renderedCleanShadowMetrics.haloRiskFraction = 0.01f;
    renderedCleanShadowMetrics.shadowTextureRisk = 0.84f;
    renderedCleanShadowMetrics.localShadowPressure = 0.56f;
    renderedCleanShadowMetrics.localHighlightPressure = 0.05f;
    renderedCleanShadowMetrics.localContrastPeak = 0.34f;
    renderedCleanShadowMetrics.centerMeanLuma = 0.20f;
    renderedCleanShadowMetrics.centerShadowFraction = 0.42f;
    renderedCleanShadowMetrics.centerHighlightFraction = 0.0f;
    std::string renderedCleanShadowReason;
    const std::string renderedCleanShadowIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            renderedCleanShadowMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            renderedCleanShadowReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics localSpatialShadowRiskMetrics = visualRiskMetrics;
    localSpatialShadowRiskMetrics.meanLuma = 0.30f;
    localSpatialShadowRiskMetrics.medianLuma = 0.28f;
    localSpatialShadowRiskMetrics.shadowFraction = 0.22f;
    localSpatialShadowRiskMetrics.highlightFraction = 0.04f;
    localSpatialShadowRiskMetrics.clippedFraction = 0.0f;
    localSpatialShadowRiskMetrics.haloRiskFraction = 0.0f;
    localSpatialShadowRiskMetrics.shadowTextureRisk = 0.60f;
    localSpatialShadowRiskMetrics.localShadowPressure = 0.70f;
    localSpatialShadowRiskMetrics.localHighlightPressure = 0.05f;
    localSpatialShadowRiskMetrics.localContrastPeak = 0.36f;
    localSpatialShadowRiskMetrics.localDamageRiskPeak = 0.76f;
    localSpatialShadowRiskMetrics.localDamageRiskMean = 0.16f;
    localSpatialShadowRiskMetrics.centerShadowFraction = 0.24f;
    std::string localSpatialShadowRiskReason;
    const std::string localSpatialShadowRiskIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localSpatialShadowRiskMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localSpatialShadowRiskReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics localSpatialFlatRiskMetrics = visualRiskMetrics;
    localSpatialFlatRiskMetrics.meanLuma = 0.36f;
    localSpatialFlatRiskMetrics.medianLuma = 0.34f;
    localSpatialFlatRiskMetrics.contrastSpan = 0.28f;
    localSpatialFlatRiskMetrics.lowSaturationFraction = 0.72f;
    localSpatialFlatRiskMetrics.highlightFraction = 0.06f;
    localSpatialFlatRiskMetrics.clippedFraction = 0.0f;
    localSpatialFlatRiskMetrics.localLumaSpread = 0.20f;
    localSpatialFlatRiskMetrics.localContrastPeak = 0.18f;
    localSpatialFlatRiskMetrics.localHighlightPressure = 0.04f;
    localSpatialFlatRiskMetrics.localShadowPressure = 0.12f;
    localSpatialFlatRiskMetrics.localDamageRiskPeak = 0.74f;
    localSpatialFlatRiskMetrics.localDamageRiskMean = 0.14f;
    std::string localSpatialFlatRiskReason;
    const std::string localSpatialFlatRiskIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            localSpatialFlatRiskMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            localSpatialFlatRiskReason);
    EditorRenderWorker::DevelopCandidateRenderMetrics renderedPreserveTextureMetrics = visualRiskMetrics;
    renderedPreserveTextureMetrics.meanLuma = 0.42f;
    renderedPreserveTextureMetrics.medianLuma = 0.36f;
    renderedPreserveTextureMetrics.shadowFraction = 0.10f;
    renderedPreserveTextureMetrics.highlightFraction = 0.06f;
    renderedPreserveTextureMetrics.clippedFraction = 0.0f;
    renderedPreserveTextureMetrics.contrastSpan = 0.40f;
    renderedPreserveTextureMetrics.lowSaturationFraction = 0.35f;
    renderedPreserveTextureMetrics.edgeContrast = 0.18f;
    renderedPreserveTextureMetrics.haloRiskFraction = 0.01f;
    renderedPreserveTextureMetrics.shadowTextureRisk = 0.12f;
    renderedPreserveTextureMetrics.localLumaSpread = 0.18f;
    renderedPreserveTextureMetrics.localContrastPeak = 0.34f;
    renderedPreserveTextureMetrics.localShadowPressure = 0.12f;
    renderedPreserveTextureMetrics.localHighlightPressure = 0.04f;
    renderedPreserveTextureMetrics.centerMeanLuma = 0.34f;
    renderedPreserveTextureMetrics.centerShadowFraction = 0.08f;
    renderedPreserveTextureMetrics.centerHighlightFraction = 0.0f;
    std::string renderedPreserveTextureReason;
    const std::string renderedPreserveTextureIntent =
        EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
            renderedPreserveTextureMetrics,
            EditorNodeGraph::DevelopAutoIntent::NaturalFinished,
            renderedPreserveTextureReason);
    const bool renderedLocalRefineIntentWorks =
        localCenterShadowIntent == "openShadows" &&
        localHighlightIntent == "protectHighlights" &&
        structuredHighlightPressureIntent == "protectHighlights" &&
        localSpatialHighlightRiskIntent == "protectHighlights" &&
        localFlatIntent == "addContrast" &&
        renderedCleanShadowIntent == "cleanShadows" &&
        localSpatialShadowRiskIntent == "cleanShadows" &&
        localSpatialFlatRiskIntent == "addContrast" &&
        renderedPreserveTextureIntent == "preserveTexture" &&
        !localCenterShadowReason.empty() &&
        !localHighlightReason.empty() &&
        structuredHighlightPressureReason.find("structured") != std::string::npos &&
        localSpatialHighlightRiskReason.find("spatial") != std::string::npos &&
        !localFlatReason.empty() &&
        !renderedCleanShadowReason.empty() &&
        localSpatialShadowRiskReason.find("spatial") != std::string::npos &&
        localSpatialFlatRiskReason.find("spatial") != std::string::npos &&
        !renderedPreserveTextureReason.empty();
    EditorRenderWorker::DevelopCandidateRenderMetrics relativeSelectedMetrics = visualRiskMetrics;
    relativeSelectedMetrics.meanLuma = 0.42f;
    relativeSelectedMetrics.medianLuma = 0.40f;
    relativeSelectedMetrics.shadowFraction = 0.16f;
    relativeSelectedMetrics.highlightFraction = 0.62f;
    relativeSelectedMetrics.clippedFraction = 0.028f;
    relativeSelectedMetrics.contrastSpan = 0.48f;
    relativeSelectedMetrics.edgeContrast = 0.22f;
    relativeSelectedMetrics.haloRiskFraction = 0.02f;
    relativeSelectedMetrics.shadowTextureRisk = 0.18f;
    relativeSelectedMetrics.localHighlightPressure = 0.66f;
    relativeSelectedMetrics.localShadowPressure = 0.16f;
    relativeSelectedMetrics.localDamageRiskPeak = 0.42f;
    relativeSelectedMetrics.localDamageRiskMean = 0.10f;
    relativeSelectedMetrics.centerHighlightFraction = 0.24f;
    relativeSelectedMetrics.centerShadowFraction = 0.08f;
    EditorRenderWorker::DevelopCandidateRenderMetrics relativeRawScoreWinnerMetrics = relativeSelectedMetrics;
    relativeRawScoreWinnerMetrics.meanLuma = 0.48f;
    relativeRawScoreWinnerMetrics.medianLuma = 0.47f;
    relativeRawScoreWinnerMetrics.highlightFraction = 0.76f;
    relativeRawScoreWinnerMetrics.clippedFraction = 0.075f;
    relativeRawScoreWinnerMetrics.localHighlightPressure = 0.76f;
    relativeRawScoreWinnerMetrics.localDamageRiskPeak = 0.58f;
    relativeRawScoreWinnerMetrics.centerHighlightFraction = 0.36f;
    EditorRenderWorker::DevelopCandidateRenderMetrics relativeIntentWinnerMetrics = relativeSelectedMetrics;
    relativeIntentWinnerMetrics.meanLuma = 0.40f;
    relativeIntentWinnerMetrics.medianLuma = 0.39f;
    relativeIntentWinnerMetrics.highlightFraction = 0.42f;
    relativeIntentWinnerMetrics.clippedFraction = 0.004f;
    relativeIntentWinnerMetrics.localHighlightPressure = 0.34f;
    relativeIntentWinnerMetrics.localDamageRiskPeak = 0.30f;
    relativeIntentWinnerMetrics.centerHighlightFraction = 0.10f;
    std::string relativeRawScoreStatus;
    std::string relativeRawScoreMetric;
    float relativeRawScoreDistance = -1.0f;
    float relativeRawScoreRepairDelta = 0.0f;
    float relativeRawScoreRepairBonus = 0.0f;
    float relativeRawScoreRegressionPenalty = 0.0f;
    const float relativeAdjustedRawScore =
        EditorModule::ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
            relativeRawScoreWinnerMetrics,
            0.74f,
            relativeSelectedMetrics,
            0.66f,
            "protectHighlights",
            relativeRawScoreStatus,
            relativeRawScoreMetric,
            relativeRawScoreDistance,
            relativeRawScoreRepairDelta,
            relativeRawScoreRepairBonus,
            relativeRawScoreRegressionPenalty);
    std::string relativeIntentStatus;
    std::string relativeIntentMetric;
    float relativeIntentDistance = -1.0f;
    float relativeIntentRepairDelta = 0.0f;
    float relativeIntentRepairBonus = 0.0f;
    float relativeIntentRegressionPenalty = 0.0f;
    const float relativeAdjustedIntentScore =
        EditorModule::ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
            relativeIntentWinnerMetrics,
            0.70f,
            relativeSelectedMetrics,
            0.66f,
            "protectHighlights",
            relativeIntentStatus,
            relativeIntentMetric,
            relativeIntentDistance,
            relativeIntentRepairDelta,
            relativeIntentRepairBonus,
            relativeIntentRegressionPenalty);
    const bool renderedRelativeComparisonWorks =
        relativeAdjustedIntentScore > relativeAdjustedRawScore + 0.035f &&
        relativeIntentStatus == "improvesActiveRepair" &&
        (relativeRawScoreStatus == "missedActiveRepair" ||
         relativeRawScoreStatus == "regressedAgainstSelected") &&
        relativeIntentMetric == "highlightPressure" &&
        relativeIntentRepairDelta > 0.05f &&
        relativeRawScoreRepairDelta < -0.05f &&
        relativeRawScoreRegressionPenalty > relativeRawScoreRepairBonus;
    auto renderedMetricsJson = [](const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics) {
        return nlohmann::json{
            { "meanLuma", metrics.meanLuma },
            { "medianLuma", metrics.medianLuma },
            { "p10Luma", metrics.p10Luma },
            { "p90Luma", metrics.p90Luma },
            { "shadowFraction", metrics.shadowFraction },
            { "highlightFraction", metrics.highlightFraction },
            { "clippedFraction", metrics.clippedFraction },
            { "contrastSpan", metrics.contrastSpan },
            { "meanRed", metrics.meanRed },
            { "meanGreen", metrics.meanGreen },
            { "meanBlue", metrics.meanBlue },
            { "warmCoolBias", metrics.warmCoolBias },
            { "magentaGreenBias", metrics.magentaGreenBias },
            { "channelImbalance", metrics.channelImbalance },
            { "colorCastRisk", metrics.colorCastRisk },
            { "meanSaturation", metrics.meanSaturation },
            { "lowSaturationFraction", metrics.lowSaturationFraction },
            { "highlightBandFraction", metrics.highlightBandFraction },
            { "highlightMeanLuma", metrics.highlightMeanLuma },
            { "highlightLowSaturationFraction", metrics.highlightLowSaturationFraction },
            { "highlightGrayRisk", metrics.highlightGrayRisk },
            { "highlightTileCoverage", metrics.highlightTileCoverage },
            { "highlightStructureScore", metrics.highlightStructureScore },
            { "meaningfulHighlightPressure", metrics.meaningfulHighlightPressure },
            { "edgeContrast", metrics.edgeContrast },
            { "haloRiskFraction", metrics.haloRiskFraction },
            { "shadowTextureRisk", metrics.shadowTextureRisk },
            { "localMeanLuma3x3", metrics.localMeanLuma },
            { "localContrastSpan3x3", metrics.localContrastSpan },
            { "localDamageRiskScore3x3", metrics.localDamageRiskScore },
            { "localLumaSpread", metrics.localLumaSpread },
            { "localEvSpreadStops", metrics.localEvSpreadStops },
            { "localEvConflict", metrics.localEvConflict },
            { "localContrastPeak", metrics.localContrastPeak },
            { "localShadowPressure", metrics.localShadowPressure },
            { "localHighlightPressure", metrics.localHighlightPressure },
            { "localDamageRiskMean", metrics.localDamageRiskMean },
            { "localDamageRiskPeak", metrics.localDamageRiskPeak },
            { "localDamageRiskPeakTile", metrics.localDamageRiskPeakTile },
            { "localExposureHighlightCrowding", metrics.localExposureHighlightCrowding },
            { "localExposureShadowCrowding", metrics.localExposureShadowCrowding },
            { "localExposureHaloStress", metrics.localExposureHaloStress },
            { "localExposureFlatnessRisk", metrics.localExposureFlatnessRisk },
            { "localExposureDamageRisk", metrics.localExposureDamageRisk },
            { "centerMeanLuma", metrics.centerMeanLuma },
            { "centerShadowFraction", metrics.centerShadowFraction },
            { "centerHighlightFraction", metrics.centerHighlightFraction },
            { "subjectCenterPrior", metrics.subjectCenterPrior },
            { "subjectReadabilityPressure", metrics.subjectReadabilityPressure },
            { "subjectProtectionPressure", metrics.subjectProtectionPressure },
            { "subjectMoodPreservationPressure", metrics.subjectMoodPreservationPressure },
            { "subjectImportanceConfidence", metrics.subjectImportanceConfidence }
        };
    };

    std::string renderedFeedbackCandidateId;
    std::string renderedFeedbackCandidateLabel;
    std::string renderedFeedbackSecondCandidateId;
    std::string renderedFeedbackSecondCandidateLabel;
    std::string renderedFeedbackThirdCandidateId;
    std::string renderedFeedbackThirdCandidateLabel;
    if (candidateSolves.is_array()) {
        for (const nlohmann::json& candidate : candidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) == selectedCandidateId ||
                candidate.value("status", std::string()) != "survivor") {
                continue;
            }
            if (renderedFeedbackCandidateId.empty()) {
                renderedFeedbackCandidateId = candidate.value("id", std::string());
                renderedFeedbackCandidateLabel = candidate.value("label", renderedFeedbackCandidateId);
            } else if (renderedFeedbackSecondCandidateId.empty()) {
                renderedFeedbackSecondCandidateId = candidate.value("id", std::string());
                renderedFeedbackSecondCandidateLabel = candidate.value("label", renderedFeedbackSecondCandidateId);
            } else {
                renderedFeedbackThirdCandidateId = candidate.value("id", std::string());
                renderedFeedbackThirdCandidateLabel = candidate.value("label", renderedFeedbackThirdCandidateId);
                break;
            }
        }
    }
    EditorNodeGraph::RawDevelopPayload renderedFeedbackPayload = payload;
    const std::uint64_t renderedFeedbackFingerprint =
        renderedFeedbackPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.86f;
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedStageBoundarySignal"] = "finishToneOnly";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedRevisionStage"] = "finishTone";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedRevisionReason"] =
            "Synthetic validation: final rendered metrics changed while pre-finish stayed stable.";
        renderedFeedbackPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.54f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.86f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedFeedbackPayload, metadata, true);
    const bool renderedFeedbackAdoptedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetrics" &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == renderedFeedbackCandidateId &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedFeedbackPayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool renderedFeedbackRevisionStageWritten =
        !renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()).empty() &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) != "none" &&
        !renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionReason", std::string()).empty();
    const bool renderedFeedbackFinishToneStageOverride =
        !renderedFeedbackCandidateId.empty() &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "finishTone" &&
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionReason", std::string()).find("pre-finish stayed stable") != std::string::npos;
    const nlohmann::json renderedFeedbackLoop =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedFeedbackLoopActive =
        renderedFeedbackLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedFeedbackLoop.value("state", std::string()) == "active" &&
        renderedFeedbackLoop.value("action", std::string()) == "adopted" &&
        renderedFeedbackLoop.value("nextStep", std::string()) == "renderUpdatedSolve" &&
        renderedFeedbackLoop.value("requiresRenderedMetrics", false) &&
        !renderedFeedbackLoop.value("requiresAutoSolve", true) &&
        renderedFeedbackLoop.value("pass", -1) == 1 &&
        renderedFeedbackLoop.value("maxPasses", 0) == 3 &&
        renderedFeedbackLoop.value("appliedRenderedFingerprint", static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint;
    const nlohmann::json renderedFeedbackContinuation =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const nlohmann::json renderedFeedbackLoopContinuation =
        renderedFeedbackLoop.value("continuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyContinues =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationVersion", std::string()) == "RenderedContinuationV1" &&
        renderedFeedbackContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedFeedbackContinuation.value("decision", std::string()) == "continue" &&
        renderedFeedbackContinuation.value("reason", std::string()) == "adopted" &&
        renderedFeedbackContinuation.value("nextStep", std::string()) == "renderUpdatedSolve" &&
        renderedFeedbackContinuation.value("requiresRenderedMetrics", false) &&
        !renderedFeedbackContinuation.value("requiresAutoSolve", true) &&
        renderedFeedbackContinuation.value("shouldContinue", false) &&
        renderedFeedbackContinuation.value("pass", -1) == 1 &&
        renderedFeedbackContinuation.value("remainingPasses", -1) == 2 &&
        renderedFeedbackContinuation.value("stageFocus", std::string()) == "finishTone" &&
        renderedFeedbackLoopContinuation.value("decision", std::string()) == "continue";
    const nlohmann::json renderedFeedbackConvergenceEvidence =
        renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceContinuesAfterFeedback =
        renderedFeedbackConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedFeedbackConvergenceEvidence.value("state", std::string()) == "continuing" &&
        renderedFeedbackConvergenceEvidence.value("decision", std::string()) == "continue" &&
        renderedFeedbackConvergenceEvidence.value("reason", std::string()) == "adopted" &&
        renderedFeedbackConvergenceEvidence.value("shouldContinue", false) &&
        renderedFeedbackConvergenceEvidence.value("requiresRenderedMetrics", false) &&
        !renderedFeedbackConvergenceEvidence.value("requiresAutoSolve", true) &&
        renderedFeedbackConvergenceEvidence.value("pass", -1) == 1 &&
        renderedFeedbackConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "continue" &&
        renderedFeedbackConvergenceEvidence.value("rendered", nlohmann::json::object()).value("feedbackApplied", false) &&
        renderedFeedbackConvergenceEvidence.value("rendered", nlohmann::json::object()).value("revisionStage", std::string()) == "finishTone";

    EditorNodeGraph::RawDevelopPayload convergenceAdmissionPayload = payload;
    convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
    convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateConvergenceEvidence"] = {
        { "version", "ConvergenceEvidenceV1" },
        { "state", "awaitingRenderedMetrics" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "shouldContinue", true },
        { "pass", 1 },
        { "nextPass", 1 },
        { "maxPasses", 3 }
    };
    convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "waitForRenderedMetrics" },
        { "reason", "awaitingRenderedMetrics" },
        { "nextStep", "renderCandidates" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "shouldContinue", true },
        { "pass", 1 },
        { "nextPass", 1 },
        { "maxPasses", 3 }
    };
    const std::string convergenceAdmissionSelectedId =
        convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    std::string convergenceAdmissionChallengerId;
    std::string convergenceAdmissionChallengerLabel;
    const nlohmann::json convergenceAdmissionCandidates =
        convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (convergenceAdmissionCandidates.is_array()) {
        for (const nlohmann::json& candidate : convergenceAdmissionCandidates) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            if (!id.empty() &&
                id != convergenceAdmissionSelectedId &&
                candidate.value("status", std::string()) == "survivor") {
                convergenceAdmissionChallengerId = id;
                convergenceAdmissionChallengerLabel = candidate.value("label", id);
                break;
            }
        }
    }
    const std::uint64_t convergenceAdmissionFingerprint =
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!convergenceAdmissionSelectedId.empty() &&
        !convergenceAdmissionChallengerId.empty() &&
        convergenceAdmissionFingerprint != 0) {
        constexpr float kAdmissionSelectedScore = 0.700f;
        constexpr float kAdmissionMarginalBestScore = 0.729f;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] =
            convergenceAdmissionFingerprint;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] =
            convergenceAdmissionChallengerId;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            convergenceAdmissionChallengerLabel;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] =
            kAdmissionMarginalBestScore;
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackAppliedFingerprint"] =
            static_cast<std::uint64_t>(0);
        convergenceAdmissionPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] =
            nlohmann::json::array({
                {
                    { "id", convergenceAdmissionSelectedId },
                    { "label", convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", convergenceAdmissionSelectedId) },
                    { "success", true },
                    { "renderScore", kAdmissionSelectedScore }
                },
                {
                    { "id", convergenceAdmissionChallengerId },
                    { "label", convergenceAdmissionChallengerLabel },
                    { "success", true },
                    { "renderScore", kAdmissionMarginalBestScore }
                }
            });
    }
    EditorModule::ApplyDevelopAutoSolve(convergenceAdmissionPayload, metadata, true);
    const nlohmann::json convergenceAdmissionEvidence =
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceEvidence",
            nlohmann::json::object());
    const nlohmann::json convergenceAdmissionRecord =
        convergenceAdmissionEvidence.value("admission", nlohmann::json::object());
    const bool convergenceEvidenceAdmissionTightensMarginalContinuation =
        !convergenceAdmissionChallengerId.empty() &&
        !convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackStopReason",
            std::string()) == "convergenceAdmissionNoMeaningfulImprovement" &&
        convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionVersion",
            std::string()) == "ConvergenceAdmissionV1" &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionTightened",
            false) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionMinimumImprovement",
            0.0f) >
            convergenceAdmissionPayload.integratedToneLayerJson.value(
                "autoCandidateConvergenceAdmissionBaseMinimumImprovement",
                1.0f) &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionEvidenceState",
            std::string()) == "awaitingRenderedMetrics" &&
        convergenceAdmissionPayload.integratedToneLayerJson.value(
            "autoCandidateConvergenceAdmissionEvidenceDecision",
            std::string()) == "waitForRenderedMetrics" &&
        convergenceAdmissionEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        convergenceAdmissionEvidence.value("state", std::string()) == "converged" &&
        convergenceAdmissionEvidence.value("reason", std::string()) == "convergenceAdmissionNoMeaningfulImprovement" &&
        convergenceAdmissionRecord.value("version", std::string()) == "ConvergenceAdmissionV1" &&
        convergenceAdmissionRecord.value("tightened", false);

    EditorNodeGraph::RawDevelopPayload continuationBiasPayload = renderedFeedbackPayload;
    EditorModule::ApplyDevelopAutoSolve(continuationBiasPayload, metadata, true);
    const bool continuationBiasDiagnosticsWritten =
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasVersion", std::string()) == "ContinuationCandidateBiasV1" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasActive", false) &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasDecision", std::string()) == "continue" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasReason", std::string()) == "responsibleStage" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasStage", std::string()) == "finishTone" &&
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasAppliedCount", 0) > 0;
    bool continuationBiasBoostsFinishToneCandidates = false;
    const nlohmann::json continuationBiasCandidates =
        continuationBiasPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (continuationBiasCandidates.is_array()) {
        for (const nlohmann::json& candidate : continuationBiasCandidates) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const bool isFinishToneCandidate =
                id == "strongerContrast" ||
                id == "toneSofterRolloff" ||
                id == "brightHighlightRolloff" ||
                id == "luminousHighlightAnchor" ||
                id == "naturalContrastGuard" ||
                id == "tonePunchierShape" ||
                id == "toneFlatterEditing" ||
                id == "toneDarkerToe" ||
                id == "renderedLocalContrastShape";
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json continuationBias =
                scoreComponents.value("renderedContinuationBias", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            continuationBiasBoostsFinishToneCandidates =
                continuationBiasBoostsFinishToneCandidates ||
                (isFinishToneCandidate &&
                 candidate.value("continuationBiasVersion", std::string()) == "ContinuationCandidateBiasV1" &&
                 candidate.value("continuationBiasStage", std::string()) == "finishTone" &&
                 candidate.value("continuationBiasBonus", 0.0f) > 0.0f &&
                 continuationBias.value("active", false) &&
                 continuationBias.value("bonus", 0.0f) > 0.0f &&
                 dimensions.value("renderedContinuationFit", 0.0f) > 0.50f);
        }
    }

    EditorNodeGraph::RawDevelopPayload continuationExpansionPayload;
    continuationExpansionPayload.scenePrepEnabled = true;
    continuationExpansionPayload.integratedToneEnabled = true;
    continuationExpansionPayload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    continuationExpansionPayload.integratedToneLayerJson = ToneCurveLayer().Serialize();
    continuationExpansionPayload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.16f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.34f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.62f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.0f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.14f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.05f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.30f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.40f;
    continuationExpansionPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.0f;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.02f;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.02f;
    continuationExpansionPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.02f;
    continuationExpansionPayload.integratedToneLayerJson["autoCandidateRenderedContinuationPolicy"] = {
        { "version", "RenderedContinuationV1" },
        { "decision", "continue" },
        { "reason", "adopted" },
        { "nextStep", "renderUpdatedSolve" },
        { "requiresRenderedMetrics", true },
        { "requiresAutoSolve", false },
        { "shouldContinue", true },
        { "pass", 1 },
        { "remainingPasses", 2 },
        { "stageFocus", "finishTone" }
    };
    continuationExpansionPayload.integratedToneLayerJson["autoCandidateRenderedRevisionStage"] = "finishTone";
    EditorModule::ApplyDevelopAutoSolve(continuationExpansionPayload, metadata, true);
    const bool continuationExpansionDiagnosticsWritten =
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionVersion",
            std::string()) == "ContinuationCandidateExpansionV1" &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionEligible",
            false) &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionActive",
            false) &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionStage",
            std::string()) == "finishTone" &&
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateContinuationExpansionAddedCount",
            0) > 0;
    bool continuationExpansionAddsFinishToneFamily = false;
    const nlohmann::json continuationExpansionCandidates =
        continuationExpansionPayload.integratedToneLayerJson.value(
            "autoCandidateSolves",
            nlohmann::json::array());
    if (continuationExpansionCandidates.is_array()) {
        for (const nlohmann::json& candidate : continuationExpansionCandidates) {
            if (!candidate.is_object()) {
                continue;
            }
            const std::string id = candidate.value("id", std::string());
            const bool isExpandedFinishToneCandidate =
                id == "toneSofterRolloff" ||
                id == "brightHighlightRolloff" ||
                id == "luminousHighlightAnchor" ||
                id == "naturalContrastGuard" ||
                id == "tonePunchierShape" ||
                id == "toneFlatterEditing" ||
                id == "toneDarkerToe";
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json expansion =
                scoreComponents.value("renderedContinuationExpansion", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            continuationExpansionAddsFinishToneFamily =
                continuationExpansionAddsFinishToneFamily ||
                (isExpandedFinishToneCandidate &&
                 candidate.value("continuationExpansionVersion", std::string()) == "ContinuationCandidateExpansionV1" &&
                 candidate.value("continuationExpansionStage", std::string()) == "finishTone" &&
                 expansion.value("active", false) &&
                 expansion.value("stageFocus", std::string()) == "finishTone" &&
                 dimensions.value("renderedContinuationCoverage", 0.0f) >= 1.0f);
        }
    }

    EditorNodeGraph::RawDevelopPayload renderedMergePayload = payload;
    if (!renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.76f;
        renderedMergePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.70f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.76f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedMergePayload, metadata, true);
    const bool renderedFeedbackMergedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsMerge" &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedFeedbackMerge" &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateMergeApplied", false) &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string()) == selectedCandidateId &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string()) == renderedFeedbackCandidateId &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "merged" &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackMerged", false) &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedMergePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";

    EditorNodeGraph::RawDevelopPayload renderedPairMergePayload = payload;
    if (!renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        renderedFeedbackFingerprint != 0) {
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.78f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSuggested"] = true;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeFirstId"] = renderedFeedbackCandidateId;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeFirstLabel"] = renderedFeedbackCandidateLabel;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeFirstScore"] = 0.78f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSecondId"] = renderedFeedbackSecondCandidateId;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSecondLabel"] = renderedFeedbackSecondCandidateLabel;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeSecondScore"] = 0.72f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedMergeMetricDistance"] = 0.22f;
        renderedPairMergePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.56f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.78f }
            },
            {
                { "id", renderedFeedbackSecondCandidateId },
                { "label", renderedFeedbackSecondCandidateLabel },
                { "success", true },
                { "renderScore", 0.72f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedPairMergePayload, metadata, true);
    const bool renderedFeedbackPairMergedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsPairMerge" &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedFeedbackPairMerge" &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeApplied", false) &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string()) == renderedFeedbackCandidateId &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string()) == renderedFeedbackSecondCandidateId &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "merged" &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackMerged", false) &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedPairMergePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";

    EditorNodeGraph::RawDevelopPayload renderedEnsembleMergePayload = payload;
    if (!renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        !renderedFeedbackThirdCandidateId.empty() &&
        renderedFeedbackFingerprint != 0) {
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.80f;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeSuggested"] = true;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeIds"] =
            nlohmann::json::array({
                renderedFeedbackCandidateId,
                renderedFeedbackSecondCandidateId,
                renderedFeedbackThirdCandidateId
            });
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeLabels"] =
            nlohmann::json::array({
                renderedFeedbackCandidateLabel,
                renderedFeedbackSecondCandidateLabel,
                renderedFeedbackThirdCandidateLabel
            });
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeScores"] =
            nlohmann::json::array({ 0.80f, 0.74f, 0.68f });
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeMetricSpread"] = 0.18f;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedEnsembleMergeScoreSpread"] = 0.12f;
        renderedEnsembleMergePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.58f }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.80f }
            },
            {
                { "id", renderedFeedbackSecondCandidateId },
                { "label", renderedFeedbackSecondCandidateLabel },
                { "success", true },
                { "renderScore", 0.74f }
            },
            {
                { "id", renderedFeedbackThirdCandidateId },
                { "label", renderedFeedbackThirdCandidateLabel },
                { "success", true },
                { "renderScore", 0.68f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedEnsembleMergePayload, metadata, true);
    const float renderedEnsembleFirstWeight =
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstWeight", 0.0f);
    const float renderedEnsembleSecondWeight =
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondWeight", 0.0f);
    const float renderedEnsembleThirdWeight =
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeThirdWeight", 0.0f);
    const bool renderedFeedbackEnsembleMergedCandidate =
        !renderedFeedbackCandidateId.empty() &&
        !renderedFeedbackSecondCandidateId.empty() &&
        !renderedFeedbackThirdCandidateId.empty() &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsEnsembleMerge" &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedFeedbackEnsembleMerge" &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeApplied", false) &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string()) == renderedFeedbackCandidateId &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string()) == renderedFeedbackSecondCandidateId &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeThirdId", std::string()) == renderedFeedbackThirdCandidateId &&
        renderedEnsembleFirstWeight > 0.20f &&
        renderedEnsembleSecondWeight > 0.20f &&
        renderedEnsembleThirdWeight > 0.20f &&
        std::fabs((renderedEnsembleFirstWeight + renderedEnsembleSecondWeight + renderedEnsembleThirdWeight) - 1.0f) < 0.01f &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "merged" &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackMerged", false) &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";

    EditorNodeGraph::RawDevelopPayload renderedMergeContinuationPayload = renderedMergePayload;
    EditorModule::ApplyDevelopAutoSolve(renderedMergeContinuationPayload, metadata, true);
    bool renderedFeedbackCandidateCarriedForward = false;
    const nlohmann::json renderedMergeContinuationCandidates =
        renderedMergeContinuationPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedMergeContinuationCandidates.is_array()) {
        for (const nlohmann::json& candidate : renderedMergeContinuationCandidates) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == "renderedFeedbackMerge" &&
                candidate.value("reason", std::string()).find("Preserved the prior authored candidate") != std::string::npos) {
                renderedFeedbackCandidateCarriedForward = true;
                break;
            }
        }
    }

    EditorNodeGraph::RawDevelopPayload renderedSurvivorCarryPayload = payload;
    const std::string priorRenderedSurvivorId = "previousRenderedSurvivorProbe";
    nlohmann::json priorCandidateSolves =
        renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (!priorCandidateSolves.is_array()) {
        priorCandidateSolves = nlohmann::json::array();
    }
    priorCandidateSolves.push_back({
        { "id", priorRenderedSurvivorId },
        { "label", "Previous Rendered Survivor Probe" },
        { "reason", "Synthetic prior rendered survivor for carry-forward validation." },
        { "score", 0.63f },
        { "status", "survivor" },
        { "guidance", {
            { "autoStrength", payload.autoGuidance.autoStrength },
            { "brightnessIntent", -0.66f },
            { "dynamicRange", 2.72f },
            { "shadowLift", 1.05f },
            { "highlightGuard", 1.12f },
            { "highlightCharacter", -0.52f },
            { "contrastBias", -0.48f }
        } }
    });
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateSolves"] = std::move(priorCandidateSolves);
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackApplied"] = true;
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "pending";
    renderedSurvivorCarryPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
        {
            { "id", priorRenderedSurvivorId },
            { "label", "Previous Rendered Survivor Probe" },
            { "success", true },
            { "renderScore", 0.74f },
            { "renderedStatus", "survivor" }
        }
    });
    EditorModule::ApplyDevelopAutoSolve(renderedSurvivorCarryPayload, metadata, true);
    bool renderedSurvivorCandidateCarriedForward = false;
    const nlohmann::json renderedSurvivorCarryCandidates =
        renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedSurvivorCarryCandidates.is_array()) {
        for (const nlohmann::json& candidate : renderedSurvivorCarryCandidates) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == priorRenderedSurvivorId &&
                candidate.value("reason", std::string()).find("actual rendered survivor") != std::string::npos &&
                candidate.value("status", std::string()) != "rejectedDuplicate" &&
                candidate.value("status", std::string()) != "rejectedDamage") {
                renderedSurvivorCandidateCarriedForward = true;
                break;
            }
        }
    }
    const bool renderedSurvivorCarryForwardCountWritten =
        renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateRenderedCarriedForwardCount", 0) >= 1;

    EditorNodeGraph::RawDevelopPayload renderedRefinePayload = payload;
    if (!selectedCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = selectedCandidateId;
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId);
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.53f;
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "brightenMids";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics showed the selected result landing too dark without highlight pressure, so the solver should try brighter mids.";
        renderedRefinePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.53f },
                { "metrics", {
                    { "meanLuma", 0.14f },
                    { "medianLuma", 0.13f },
                    { "p10Luma", 0.03f },
                    { "p90Luma", 0.46f },
                    { "shadowFraction", 0.58f },
                    { "highlightFraction", 0.04f },
                    { "clippedFraction", 0.0f },
                    { "contrastSpan", 0.43f }
                } }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedRefinePayload, metadata, true);
    bool renderedLocalRefineCandidateGenerated = false;
    const nlohmann::json renderedRefineCandidateSolves =
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedRefineCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : renderedRefineCandidateSolves) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == "renderedLocalBrightenMids" &&
                candidate.value("reason", std::string()).find("Generated a rendered-local candidate family") != std::string::npos) {
                renderedLocalRefineCandidateGenerated = true;
                break;
            }
        }
    }
    const bool renderedFeedbackRefinedCandidate =
        !selectedCandidateId.empty() &&
        renderedLocalRefineCandidateGenerated &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsRefine" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedLocalBrightenMids" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "refined" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) == "brightenMids" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedRefinePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool renderedRefineRevisionStageIsScenePrep =
        renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "scenePrep";
    const bool renderedRefineStagePlanTargetsScenePrep =
        renderedRefinePayload.integratedToneLayerJson.value("autoStageSolveVersion", std::string()) == "StagedAutoSolveV1" &&
        renderedRefinePayload.integratedToneLayerJson.value("autoStageResponsibleRevisionState", std::string()) == "SOLVE_SCENE_PREP" &&
        !renderedRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string()).empty() &&
        renderedRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string()) == "scenePrep";

    EditorNodeGraph::RawDevelopPayload renderedCleanupRefinePayload = payload;
    if (!selectedCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = selectedCandidateId;
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId);
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.55f;
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "cleanShadows";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics showed shadow texture/noise pressure without matching highlight trouble, so the solver should try a cleaner-shadow refinement instead of simply lifting more.";
        renderedCleanupRefinePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.55f },
                { "metrics", renderedMetricsJson(renderedCleanShadowMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedCleanupRefinePayload, metadata, true);
    bool renderedCleanupRefineCandidateGenerated = false;
    const nlohmann::json renderedCleanupRefineCandidateSolves =
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (renderedCleanupRefineCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : renderedCleanupRefineCandidateSolves) {
            if (candidate.is_object() &&
                candidate.value("id", std::string()) == "renderedLocalCleanShadows" &&
                candidate.value("reason", std::string()).find("Generated a rendered-local candidate family") != std::string::npos) {
                renderedCleanupRefineCandidateGenerated = true;
                break;
            }
        }
    }
    const bool renderedCleanupRefinedCandidate =
        !selectedCandidateId.empty() &&
        renderedCleanupRefineCandidateGenerated &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) == "renderedMetricsRefine" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string()) == "renderedLocalCleanShadows" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string()) == "refined" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) == "cleanShadows" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", false) &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFeedbackFingerprint &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool renderedCleanupRevisionStageIsRawCleanup =
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "rawCleanup";
    const bool renderedCleanupStagePlanTargetsRawBase =
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageSolveVersion", std::string()) == "StagedAutoSolveV1" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string()) == "RENDER_RAW_BASE" &&
        renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string()) == "rawCleanup";

    EditorNodeGraph::RawDevelopPayload renderedRepeatedRefinePayload = renderedRefinePayload;
    const std::string renderedRefineSelectedId =
        renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t renderedRefineFingerprint =
        renderedRepeatedRefinePayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedRefineSelectedId.empty() && renderedRefineFingerprint != 0) {
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedRefineFingerprint;
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedRefineSelectedId;
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedRefineSelectedId);
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.535f;
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "brightenMids";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics still looked dark, but the previous refinement did not improve enough.";
        renderedRepeatedRefinePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", "mergedAutoPick" },
                { "label", "Merged Auto Pick" },
                { "success", true },
                { "renderScore", 0.535f }
            },
            {
                { "id", renderedRefineSelectedId },
                { "label", renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedRefineSelectedId) },
                { "success", true },
                { "renderScore", 0.535f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedRepeatedRefinePayload, metadata, true);
    const bool renderedRepeatedRefineStops =
        !renderedRefineSelectedId.empty() &&
        !renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) != "renderedMetricsRefine";
    EditorNodeGraph::RawDevelopPayload renderedMonotonicShadowPayload = renderedCleanupRefinePayload;
    EditorRenderWorker::DevelopCandidateRenderMetrics previousShadowRefineMetrics = renderedCleanShadowMetrics;
    previousShadowRefineMetrics.shadowTextureRisk = 0.54f;
    previousShadowRefineMetrics.localShadowPressure = 0.46f;
    previousShadowRefineMetrics.localDamageRiskPeak = 0.38f;
    previousShadowRefineMetrics.localDamageRiskMean = 0.12f;
    EditorRenderWorker::DevelopCandidateRenderMetrics currentShadowRefineMetrics = previousShadowRefineMetrics;
    currentShadowRefineMetrics.shadowTextureRisk = 0.66f;
    currentShadowRefineMetrics.localShadowPressure = 0.60f;
    currentShadowRefineMetrics.localDamageRiskPeak = 0.58f;
    currentShadowRefineMetrics.localDamageRiskMean = 0.18f;
    const std::string previousMonotonicShadowSelectedId =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t previousMonotonicShadowFingerprint =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
    renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackHistory"] = nlohmann::json::array({
        {
            { "fingerprint", static_cast<std::uint64_t>(42) },
            { "selectedId", previousMonotonicShadowSelectedId },
            { "selectedRenderScore", 0.555f },
            { "selectedRenderScoreValid", true },
            { "bestId", previousMonotonicShadowSelectedId },
            { "bestRenderScore", 0.555f },
            { "successCount", 1 },
            { "failureCount", 0 },
            { "action", "solveRequested" },
            { "stopReason", "renderedSelectedNeedsRefinement" },
            { "refineIntent", "cleanShadows" },
            { "refineReason", "Previous rendered pass requested cleaner shadows." },
            { "selectedMetrics", renderedMetricsJson(previousShadowRefineMetrics) },
            { "bestMetrics", renderedMetricsJson(previousShadowRefineMetrics) }
        }
    });
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderedVersion");
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderedFingerprint");
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderMetricsStatus");
    renderedMonotonicShadowPayload.integratedToneLayerJson.erase("autoCandidateRenderedSolves");
    EditorModule::ApplyDevelopAutoSolve(renderedMonotonicShadowPayload, metadata, true);
    const std::string renderedMonotonicShadowSelectedId =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t renderedMonotonicShadowFingerprint =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedMonotonicShadowSelectedId.empty() && renderedMonotonicShadowFingerprint != 0) {
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] =
            renderedMonotonicShadowFingerprint;
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] =
            renderedMonotonicShadowSelectedId;
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedMonotonicShadowSelectedId);
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.572f;
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedRefineIntent"] = "cleanShadows";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedRefineReason"] =
            "Rendered metrics still showed shadow texture pressure, but the same cleanup direction is making the protected risk worse.";
        renderedMonotonicShadowPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", renderedMonotonicShadowSelectedId },
                { "label", renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedMonotonicShadowSelectedId) },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "renderedLocalCleanShadows" },
                { "label", "Rendered Local Cleaner Shadows" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "cleanShadows" },
                { "label", "Clean Shadows" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "mergedAutoPick" },
                { "label", "Merged Auto Pick" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            },
            {
                { "id", "base" },
                { "label", "Base" },
                { "success", true },
                { "renderScore", 0.572f },
                { "metrics", renderedMetricsJson(currentShadowRefineMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedMonotonicShadowPayload, metadata, true);
    const nlohmann::json renderedMonotonicShadowLoop =
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedMonotonicShadowRiskStops =
        !renderedMonotonicShadowSelectedId.empty() &&
        !renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedRefineMonotonicShadowRisk" &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()) == "stopped" &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicMetric", std::string()) == "shadowTextureRisk" &&
        renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicCurrentValue", 0.0f) >
            renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicPreviousValue", 1.0f) &&
        renderedMonotonicShadowLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedMonotonicShadowLoop.value("state", std::string()) == "converged" &&
        renderedMonotonicShadowLoop.value("stopReason", std::string()) == "renderedRefineMonotonicShadowRisk" &&
        renderedMonotonicShadowLoop.value("monotonicGuardStatus", std::string()) == "stopped";
    EditorNodeGraph::RawDevelopPayload renderedNoImprovementPayload = renderedFeedbackPayload;
    const std::string renderedAdoptedSelectedId =
        renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string());
    const std::uint64_t renderedAdoptedFingerprint =
        renderedNoImprovementPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!renderedAdoptedSelectedId.empty() && renderedAdoptedFingerprint != 0) {
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedAdoptedFingerprint;
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedAdoptedSelectedId;
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] =
            renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedAdoptedSelectedId);
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.82f;
        renderedNoImprovementPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", renderedAdoptedSelectedId },
                { "label", renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedLabel", renderedAdoptedSelectedId) },
                { "success", true },
                { "renderScore", 0.82f }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedNoImprovementPayload, metadata, true);
    const bool renderedNoImprovementStops =
        !renderedAdoptedSelectedId.empty() &&
        !renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", 0) == 1 &&
        renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string()) != "renderedMetrics";
    EditorNodeGraph::RawDevelopPayload renderedStablePayload = payload;
    if (!selectedCandidateId.empty() && !renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.815f;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 1;
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedFeedbackHistory"] = nlohmann::json::array({
            {
                { "fingerprint", static_cast<std::uint64_t>(renderedFeedbackFingerprint - 1) },
                { "selectedId", selectedCandidateId },
                { "selectedRenderScore", 0.80f },
                { "selectedRenderScoreValid", true },
                { "bestId", renderedFeedbackCandidateId },
                { "bestRenderScore", 0.812f },
                { "successCount", 2 },
                { "failureCount", 0 },
                { "action", "solveRequested" },
                { "stopReason", "renderedBestImproved" },
                { "bestMetrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
        renderedStablePayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.80f },
                { "metrics", renderedMetricsJson(similarRenderedMetrics) }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.815f },
                { "metrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedStablePayload, metadata, true);
    const bool renderedStableMetricsConverge =
        !selectedCandidateId.empty() &&
        !renderedFeedbackCandidateId.empty() &&
        !renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedMetricsStable" &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityStatus", std::string()) == "stable" &&
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityDistance", 1.0f) < 0.045f;
    const nlohmann::json renderedStableLoop =
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedStableLoopConverged =
        renderedStableLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedStableLoop.value("state", std::string()) == "converged" &&
        renderedStableLoop.value("action", std::string()) == "stopped" &&
        renderedStableLoop.value("stopReason", std::string()) == "renderedMetricsStable" &&
        renderedStableLoop.value("nextStep", std::string()) == "none" &&
        !renderedStableLoop.value("requiresRenderedMetrics", true) &&
        !renderedStableLoop.value("requiresAutoSolve", true) &&
        renderedStableLoop.value("pass", -1) == 1 &&
        renderedStableLoop.value("maxPasses", 0) == 3;
    const nlohmann::json renderedStableContinuation =
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyStopsStable =
        renderedStableContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedStableContinuation.value("decision", std::string()) == "stop" &&
        renderedStableContinuation.value("reason", std::string()) == "renderedMetricsStable" &&
        renderedStableContinuation.value("nextStep", std::string()) == "none" &&
        !renderedStableContinuation.value("requiresRenderedMetrics", true) &&
        !renderedStableContinuation.value("requiresAutoSolve", true) &&
        !renderedStableContinuation.value("shouldContinue", true) &&
        renderedStableContinuation.value("pass", -1) == 1 &&
        renderedStableContinuation.value("remainingPasses", -1) == 2;
    const nlohmann::json renderedStableConvergenceEvidence =
        renderedStablePayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsStable =
        renderedStableConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedStableConvergenceEvidence.value("state", std::string()) == "converged" &&
        renderedStableConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedStableConvergenceEvidence.value("reason", std::string()) == "renderedMetricsStable" &&
        !renderedStableConvergenceEvidence.value("shouldContinue", true) &&
        !renderedStableConvergenceEvidence.value("requiresRenderedMetrics", true) &&
        renderedStableConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stopConverged", false) &&
        renderedStableConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stabilityStatus", std::string()) == "stable" &&
        renderedStableConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "stop";
    EditorNodeGraph::RawDevelopPayload renderedTrendPayload = payload;
    EditorRenderWorker::DevelopCandidateRenderMetrics trendPreviousBestMetrics = visualRiskMetrics;
    // Keep this close enough to prove a stalled trend, but far enough from
    // the current result that the earlier "stable metrics" stop does not win.
    trendPreviousBestMetrics.meanLuma += 0.060f;
    trendPreviousBestMetrics.medianLuma += 0.030f;
    if (!selectedCandidateId.empty() && !renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.795f;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 2;
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackHistory"] = nlohmann::json::array({
            {
                { "fingerprint", static_cast<std::uint64_t>(renderedFeedbackFingerprint - 2) },
                { "selectedId", selectedCandidateId },
                { "selectedRenderScore", 0.748f },
                { "selectedRenderScoreValid", true },
                { "bestId", renderedFeedbackCandidateId },
                { "bestRenderScore", 0.786f },
                { "successCount", 2 },
                { "failureCount", 0 },
                { "action", "merged" },
                { "stopReason", "" },
                { "bestMetrics", renderedMetricsJson(visualRiskMetrics) }
            },
            {
                { "fingerprint", static_cast<std::uint64_t>(renderedFeedbackFingerprint - 1) },
                { "selectedId", selectedCandidateId },
                { "selectedRenderScore", 0.752f },
                { "selectedRenderScoreValid", true },
                { "bestId", renderedFeedbackCandidateId },
                { "bestRenderScore", 0.792f },
                { "successCount", 2 },
                { "failureCount", 0 },
                { "action", "solveRequested" },
                { "stopReason", "renderedBestImproved" },
                { "bestMetrics", renderedMetricsJson(trendPreviousBestMetrics) }
            }
        });
        renderedTrendPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.750f },
                { "metrics", renderedMetricsJson(similarRenderedMetrics) }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.795f },
                { "metrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedTrendPayload, metadata, true);
    const bool renderedTrendConverges =
        !selectedCandidateId.empty() &&
        !renderedFeedbackCandidateId.empty() &&
        !renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", false) &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendStatus", std::string()) == "stalled" &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendHistoryCount", 0) >= 2 &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendSameBestCount", 0) >= 2 &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendScoreSpread", 1.0f) < 0.030f &&
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendNearestDistance", 1.0f) < 0.055f;
    const nlohmann::json renderedTrendLoop =
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedTrendLoopConverged =
        renderedTrendLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedTrendLoop.value("state", std::string()) == "converged" &&
        renderedTrendLoop.value("action", std::string()) == "stopped" &&
        renderedTrendLoop.value("stopReason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        renderedTrendLoop.value("nextStep", std::string()) == "none" &&
        !renderedTrendLoop.value("requiresRenderedMetrics", true) &&
        !renderedTrendLoop.value("requiresAutoSolve", true) &&
        renderedTrendLoop.value("pass", -1) == 2 &&
        renderedTrendLoop.value("historyCount", 0) >= 2 &&
        renderedTrendLoop.value("maxPasses", 0) == 3;
    const nlohmann::json renderedTrendContinuation =
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyStopsTrend =
        renderedTrendContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedTrendContinuation.value("decision", std::string()) == "stop" &&
        renderedTrendContinuation.value("reason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        renderedTrendContinuation.value("nextStep", std::string()) == "none" &&
        renderedTrendContinuation.value("pass", -1) == 2 &&
        renderedTrendContinuation.value("remainingPasses", -1) == 1 &&
        renderedTrendContinuation.value("evidence", nlohmann::json::object()).value("trendStatus", std::string()) == "stalled";
    const nlohmann::json renderedTrendConvergenceEvidence =
        renderedTrendPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsTrend =
        renderedTrendConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedTrendConvergenceEvidence.value("state", std::string()) == "converged" &&
        renderedTrendConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedTrendConvergenceEvidence.value("reason", std::string()) == "renderedFeedbackNoImprovementTrend" &&
        !renderedTrendConvergenceEvidence.value("shouldContinue", true) &&
        renderedTrendConvergenceEvidence.value("pass", -1) == 2 &&
        renderedTrendConvergenceEvidence.value("rendered", nlohmann::json::object()).value("trendStatus", std::string()) == "stalled" &&
        renderedTrendConvergenceEvidence.value("rendered", nlohmann::json::object()).value("trendHistoryCount", 0) >= 2 &&
        renderedTrendConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "stop";
    EditorNodeGraph::RawDevelopPayload renderedPassLimitPayload = payload;
    if (!selectedCandidateId.empty() && !renderedFeedbackCandidateId.empty() && renderedFeedbackFingerprint != 0) {
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedFeedbackFingerprint;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = renderedFeedbackCandidateId;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = renderedFeedbackCandidateLabel;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = 0.910f;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedFeedbackPass"] = 3;
        renderedPassLimitPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
            {
                { "id", selectedCandidateId },
                { "label", payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", selectedCandidateId) },
                { "success", true },
                { "renderScore", 0.720f },
                { "metrics", renderedMetricsJson(similarRenderedMetrics) }
            },
            {
                { "id", renderedFeedbackCandidateId },
                { "label", renderedFeedbackCandidateLabel },
                { "success", true },
                { "renderScore", 0.910f },
                { "metrics", renderedMetricsJson(visualRiskMetrics) }
            }
        });
    }
    EditorModule::ApplyDevelopAutoSolve(renderedPassLimitPayload, metadata, true);
    const nlohmann::json renderedPassLimitLoop =
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const nlohmann::json renderedPassLimitContinuation =
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const bool renderedContinuationPolicyStopsAtPassLimit =
        !selectedCandidateId.empty() &&
        !renderedFeedbackCandidateId.empty() &&
        !renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "renderedFeedbackPassLimit" &&
        !renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", true) &&
        renderedPassLimitLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedPassLimitLoop.value("state", std::string()) == "stopped" &&
        renderedPassLimitLoop.value("stopReason", std::string()) == "renderedFeedbackPassLimit" &&
        renderedPassLimitContinuation.value("version", std::string()) == "RenderedContinuationV1" &&
        renderedPassLimitContinuation.value("decision", std::string()) == "stop" &&
        renderedPassLimitContinuation.value("reason", std::string()) == "renderedFeedbackPassLimit" &&
        renderedPassLimitContinuation.value("pass", -1) == 3 &&
        renderedPassLimitContinuation.value("remainingPasses", -1) == 0 &&
        !renderedPassLimitContinuation.value("shouldContinue", true);
    const nlohmann::json renderedPassLimitConvergenceEvidence =
        renderedPassLimitPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsAtPassLimit =
        renderedPassLimitConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedPassLimitConvergenceEvidence.value("state", std::string()) == "stopped" &&
        renderedPassLimitConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedPassLimitConvergenceEvidence.value("reason", std::string()) == "renderedFeedbackPassLimit" &&
        !renderedPassLimitConvergenceEvidence.value("shouldContinue", true) &&
        renderedPassLimitConvergenceEvidence.value("pass", -1) == 3 &&
        renderedPassLimitConvergenceEvidence.value("remainingPasses", -1) == 0 &&
        !renderedPassLimitConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stopConverged", true) &&
        renderedPassLimitConvergenceEvidence.value("continuation", nlohmann::json::object()).value("decision", std::string()) == "stop";
    const bool renderedFeedbackStopConvergenceClassifierWorks =
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedMetricsStable") &&
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("noMeaningfulRenderedImprovement") &&
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("convergenceAdmissionNoMeaningfulImprovement") &&
        EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedRefineMonotonicShadowRisk") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("noRenderedBestCandidate") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("allRenderedCandidatesRejectedForDamage") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedBestBelowQualityFloor") &&
        !EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation("renderedFeedbackPassLimit");
    EditorNodeGraph::RawDevelopPayload renderedNoBestPayload = payload;
    const std::uint64_t renderedNoBestFingerprint =
        renderedNoBestPayload.integratedToneLayerJson.value(
            "autoCandidateSolveFingerprint",
            static_cast<std::uint64_t>(0));
    if (!selectedCandidateId.empty() && renderedNoBestFingerprint != 0) {
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedFingerprint"] = renderedNoBestFingerprint;
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedBestId"] = "";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedBestLabel"] = "";
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedBestScore"] = -1.0f;
        renderedNoBestPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] =
            nlohmann::json::array();
    }
    EditorModule::ApplyDevelopAutoSolve(renderedNoBestPayload, metadata, true);
    const nlohmann::json renderedNoBestLoop =
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackLoop", nlohmann::json::object());
    const bool renderedNoBestStopsWithoutConverging =
        !selectedCandidateId.empty() &&
        !renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true) &&
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string()) == "noRenderedBestCandidate" &&
        !renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", true) &&
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string()) == "none" &&
        renderedNoBestLoop.value("version", std::string()) == "RenderedFeedbackLoopV1" &&
        renderedNoBestLoop.value("state", std::string()) == "stopped" &&
        renderedNoBestLoop.value("stopReason", std::string()) == "noRenderedBestCandidate" &&
        renderedNoBestLoop.value("nextStep", std::string()) == "none" &&
        !renderedNoBestLoop.value("requiresAutoSolve", true) &&
        !renderedNoBestLoop.value("requiresRenderedMetrics", true);
    const nlohmann::json renderedNoBestConvergenceEvidence =
        renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    const bool convergenceEvidenceStopsNoBest =
        renderedNoBestConvergenceEvidence.value("version", std::string()) == "ConvergenceEvidenceV1" &&
        renderedNoBestConvergenceEvidence.value("state", std::string()) == "stopped" &&
        renderedNoBestConvergenceEvidence.value("decision", std::string()) == "stop" &&
        renderedNoBestConvergenceEvidence.value("reason", std::string()) == "noRenderedBestCandidate" &&
        !renderedNoBestConvergenceEvidence.value("shouldContinue", true) &&
        !renderedNoBestConvergenceEvidence.value("rendered", nlohmann::json::object()).value("stopConverged", true) &&
        renderedNoBestConvergenceEvidence.value("rendered", nlohmann::json::object()).value("revisionStage", std::string()) == "none";
    const Raw::RawDevelopSettings authoredAfterFullSolve = payload.settings;
    const Raw::RawDetailFusionSettings prepAfterFullSolve = payload.scenePrepSettings;
    payload.autoGuidance.contrastBias = 0.58f;
    const std::uint64_t partialRequestIdBefore =
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
    EditorModule::ApplyDevelopAutoSolve(payload, metadata, true, false);
    const bool partialSolvePreservedRawAuthorship =
        std::abs(payload.settings.exposureStops - authoredAfterFullSolve.exposureStops) < 0.0001f &&
        payload.settings.highlightMode == authoredAfterFullSolve.highlightMode &&
        std::abs(payload.settings.highlightStrength - authoredAfterFullSolve.highlightStrength) < 0.0001f &&
        payload.settings.mosaicDenoise.enabled == authoredAfterFullSolve.mosaicDenoise.enabled &&
        std::abs(payload.settings.mosaicDenoise.lumaStrength - authoredAfterFullSolve.mosaicDenoise.lumaStrength) < 0.0001f;
    const bool partialSolveUpdatedPrepAndFinish =
        std::abs(payload.scenePrepSettings.wellExposedTargetBias - prepAfterFullSolve.wellExposedTargetBias) > 0.001f &&
        std::abs(payload.scenePrepSettings.detailWeight - prepAfterFullSolve.detailWeight) > 0.001f &&
        std::abs(payload.integratedToneLayerJson.value("autoRequestedContrastBias", -99.0f) - payload.autoGuidance.contrastBias) < 0.0001f &&
        payload.integratedToneLayerJson.value("autoCalibratePending", false) &&
        payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0)) > partialRequestIdBefore;

    EditorNodeGraph::RawDevelopPayload balancedPayload;
    balancedPayload.integratedToneEnabled = true;
    balancedPayload.scenePrepEnabled = true;
    balancedPayload.integratedToneLayerJson = payload.integratedToneLayerJson;
    balancedPayload.autoGuidance = payload.autoGuidance;
    balancedPayload.autoGuidance.autoStrength = 1.05f;
    balancedPayload.autoGuidance.dynamicRange = 1.20f;
    balancedPayload.autoGuidance.shadowLift = 0.08f;
    balancedPayload.autoGuidance.highlightGuard = 0.10f;
    balancedPayload.autoGuidance.highlightCharacter = -0.05f;
    balancedPayload.autoGuidance.contrastBias = 0.06f;
    balancedPayload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    balancedPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.05f;
    balancedPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.18f;
    balancedPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.80f;
    balancedPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.001f;
    balancedPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.16f;
    balancedPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.24f;
    balancedPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.68f;
    balancedPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.7f;
    balancedPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    balancedPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.06f;
    balancedPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.04f;
    balancedPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.10f;
    balancedPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.08f;
    EditorModule::ApplyDevelopAutoSolve(balancedPayload, metadata, true);
    const Raw::RawDevelopSettings balancedFirstSettings = balancedPayload.settings;
    const Raw::RawDetailFusionSettings balancedFirstPrep = balancedPayload.scenePrepSettings;
    EditorModule::ApplyDevelopAutoSolve(balancedPayload, metadata, true);
    const bool repeatedFullSolveStable =
        std::abs(balancedPayload.settings.exposureStops - balancedFirstSettings.exposureStops) < 0.0001f &&
        balancedPayload.settings.highlightMode == balancedFirstSettings.highlightMode &&
        std::abs(balancedPayload.settings.highlightStrength - balancedFirstSettings.highlightStrength) < 0.0001f &&
        std::abs(balancedPayload.settings.highlightThreshold - balancedFirstSettings.highlightThreshold) < 0.0001f &&
        balancedPayload.settings.mosaicDenoise.enabled == balancedFirstSettings.mosaicDenoise.enabled &&
        std::abs(balancedPayload.settings.mosaicDenoise.lumaStrength - balancedFirstSettings.mosaicDenoise.lumaStrength) < 0.0001f &&
        std::abs(balancedPayload.scenePrepSettings.baseEvBias - balancedFirstPrep.baseEvBias) < 0.0001f &&
        std::abs(balancedPayload.scenePrepSettings.maxEvBias - balancedFirstPrep.maxEvBias) < 0.0001f &&
        std::abs(balancedPayload.scenePrepSettings.highlightProtectionBias - balancedFirstPrep.highlightProtectionBias) < 0.0001f;

    EditorNodeGraph::RawDevelopPayload neutralExposurePayload = balancedPayload;
    neutralExposurePayload.autoGuidance.exposureBias = 0.0f;
    EditorModule::ApplyDevelopAutoSolve(neutralExposurePayload, metadata, true);
    EditorNodeGraph::RawDevelopPayload biasedExposurePayload = balancedPayload;
    biasedExposurePayload.autoGuidance.exposureBias = 0.65f;
    EditorModule::ApplyDevelopAutoSolve(biasedExposurePayload, metadata, true);
    const bool positiveExposureBiasPreserved =
        biasedExposurePayload.settings.exposureStops > neutralExposurePayload.settings.exposureStops + 0.80f &&
        biasedExposurePayload.scenePrepSettings.baseEvBias > neutralExposurePayload.scenePrepSettings.baseEvBias + 0.20f;

    EditorNodeGraph::RawDevelopPayload highlightHeavyPayload = balancedPayload;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.02f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.11f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.96f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.018f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.24f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.84f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.62f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 5.8f;
    highlightHeavyPayload.integratedToneLayerJson["autoSceneProfile"] = 1;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.42f;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.18f;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.22f;
    highlightHeavyPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.40f;
    EditorModule::ApplyDevelopAutoSolve(highlightHeavyPayload, metadata, true);
    const nlohmann::json& highlightHeavyCandidateSolves =
        highlightHeavyPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (highlightHeavyCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : highlightHeavyCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "broadHighlightGuard") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            broadHighlightGuardGenerated = true;
            broadHighlightGuardEligible = broadHighlightGuardEligible || eligible;
            broadHighlightGuardGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), highlightHeavyPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            broadHighlightGuardHumanReadable =
                broadHighlightGuardHumanReadable ||
                (label.find("Broad") != std::string::npos &&
                 label.find("Highlight") != std::string::npos &&
                 reason.find("broad bright regions") != std::string::npos &&
                 reason.find("does not recover") != std::string::npos);
            broadHighlightGuardDiagnosticsWritten =
                broadHighlightGuardDiagnosticsWritten ||
                (dimensions.value("broadHighlightControl", -1.0f) > 0.54f &&
                 dimensions.value("highlightIntegrity", -1.0f) > 0.42f &&
                 changes.value("highlightGuardDelta", 0.0f) > 0.24f &&
                 changes.value("dynamicRangeDelta", 0.0f) > 0.16f);
        }
    }
    RenderGraphRawDevelopPayload broadHighlightBaseCandidateRenderPayload;
    broadHighlightBaseCandidateRenderPayload.settings = highlightHeavyPayload.settings;
    broadHighlightBaseCandidateRenderPayload.scenePrepEnabled = highlightHeavyPayload.scenePrepEnabled;
    broadHighlightBaseCandidateRenderPayload.scenePrepSettings = highlightHeavyPayload.scenePrepSettings;
    broadHighlightBaseCandidateRenderPayload.integratedToneEnabled = highlightHeavyPayload.integratedToneEnabled;
    broadHighlightBaseCandidateRenderPayload.integratedToneLayerJson = highlightHeavyPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload broadHighlightGuardPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            broadHighlightBaseCandidateRenderPayload,
            highlightHeavyPayload.autoGuidance,
            broadHighlightGuardGuidance,
            "broadHighlightGuard",
            highlightHeavyPayload.autoGuidance.intent);
    const bool broadHighlightGuardRenderPayloadConstrained =
        broadHighlightGuardGenerated &&
        broadHighlightGuardEligible &&
        broadHighlightGuardHumanReadable &&
        broadHighlightGuardDiagnosticsWritten &&
        std::abs(broadHighlightGuardPayload.settings.exposureStops -
            broadHighlightBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(broadHighlightGuardPayload.settings.highlightStrength -
            broadHighlightBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        broadHighlightGuardPayload.scenePrepSettings.minEvBias <=
            broadHighlightBaseCandidateRenderPayload.scenePrepSettings.minEvBias - 0.08f &&
        broadHighlightGuardPayload.scenePrepSettings.highlightProtectionBias >=
            broadHighlightBaseCandidateRenderPayload.scenePrepSettings.highlightProtectionBias - 0.0001f &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "broadHighlightGuard";
    const bool highlightProfilePrefersHeadroom =
        highlightHeavyPayload.settings.highlightMode == Raw::HighlightReconstructionMode::ColorReconstruction &&
        highlightHeavyPayload.settings.highlightStrength > balancedPayload.settings.highlightStrength + 0.08f &&
        highlightHeavyPayload.scenePrepSettings.highlightProtectionBias > balancedPayload.scenePrepSettings.highlightProtectionBias + 0.18f &&
        highlightHeavyPayload.settings.highlightThreshold < balancedPayload.settings.highlightThreshold - 0.03f &&
        highlightHeavyPayload.scenePrepSettings.minEvBias < balancedPayload.scenePrepSettings.minEvBias - 0.18f &&
        highlightHeavyPayload.scenePrepSettings.highlightProtection > balancedPayload.scenePrepSettings.highlightProtection + 0.06f;

    EditorNodeGraph::RawDevelopPayload specularPayload = balancedPayload;
    specularPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.05f;
    specularPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.19f;
    specularPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.89f;
    specularPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.002f;
    specularPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.12f;
    specularPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.20f;
    specularPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.72f;
    specularPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 3.2f;
    specularPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    specularPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.18f;
    specularPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.04f;
    specularPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.08f;
    specularPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    EditorModule::ApplyDevelopAutoSolve(specularPayload, metadata, true);
    const nlohmann::json& specularCandidateSolves =
        specularPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (specularCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : specularCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "specularHighlightTolerance") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            specularHighlightToleranceGenerated = true;
            specularHighlightToleranceEligible =
                specularHighlightToleranceEligible || eligible;
            specularHighlightToleranceGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), specularPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            specularHighlightToleranceHumanReadable =
                specularHighlightToleranceHumanReadable ||
                (label.find("Specular") != std::string::npos &&
                 label.find("Highlight") != std::string::npos &&
                 reason.find("tiny specular") != std::string::npos &&
                 reason.find("not clipped-data recovery") != std::string::npos);
            specularHighlightToleranceDiagnosticsWritten =
                specularHighlightToleranceDiagnosticsWritten ||
                (dimensions.value("specularTolerance", -1.0f) > 0.54f &&
                 dimensions.value("brightnessHierarchy", -1.0f) > 0.50f &&
                 changes.value("highlightCharacterDelta", 0.0f) > 0.24f &&
                 changes.value("highlightGuardDelta", 0.0f) < -0.10f);
        }
    }
    RenderGraphRawDevelopPayload specularBaseCandidateRenderPayload;
    specularBaseCandidateRenderPayload.settings = specularPayload.settings;
    specularBaseCandidateRenderPayload.scenePrepEnabled = specularPayload.scenePrepEnabled;
    specularBaseCandidateRenderPayload.scenePrepSettings = specularPayload.scenePrepSettings;
    specularBaseCandidateRenderPayload.integratedToneEnabled = specularPayload.integratedToneEnabled;
    specularBaseCandidateRenderPayload.integratedToneLayerJson = specularPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload specularHighlightTolerancePayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            specularBaseCandidateRenderPayload,
            specularPayload.autoGuidance,
            specularHighlightToleranceGuidance,
            "specularHighlightTolerance",
            specularPayload.autoGuidance.intent);
    const bool specularHighlightToleranceRenderPayloadConstrained =
        specularHighlightToleranceGenerated &&
        specularHighlightToleranceEligible &&
        specularHighlightToleranceHumanReadable &&
        specularHighlightToleranceDiagnosticsWritten &&
        std::abs(specularHighlightTolerancePayload.settings.exposureStops -
            specularBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(specularHighlightTolerancePayload.scenePrepSettings.maxEvBias -
            specularBaseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "specularHighlightTolerance" &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            specularPayload.autoGuidance.highlightCharacter + 0.24f &&
        specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightBias", 99.0f) <
            specularPayload.autoGuidance.highlightGuard - 0.10f;
    const bool tinySpecularsDoNotDragExposure =
        specularPayload.settings.exposureStops >= balancedPayload.settings.exposureStops - 0.20f &&
        specularPayload.scenePrepSettings.highlightProtectionBias < highlightHeavyPayload.scenePrepSettings.highlightProtectionBias - 0.35f &&
        specularPayload.scenePrepSettings.minEvBias > highlightHeavyPayload.scenePrepSettings.minEvBias + 0.18f;
    const bool broadHighlightsProtectedMoreThanSpeculars =
        highlightHeavyPayload.settings.exposureStops < specularPayload.settings.exposureStops - 0.20f &&
        highlightHeavyPayload.scenePrepSettings.highlightProtectionBias > specularPayload.scenePrepSettings.highlightProtectionBias + 0.30f &&
        highlightHeavyPayload.scenePrepSettings.minEvBias < specularPayload.scenePrepSettings.minEvBias - 0.20f;

    EditorNodeGraph::RawDevelopPayload flatGrayHierarchyPayload = balancedPayload;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.08f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.25f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.56f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.001f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.18f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.18f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.72f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.0f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.18f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 0.96f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.02f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.02f;
    flatGrayHierarchyPayload.integratedToneLayerJson["autoCandidateRenderMetricsStatus"] = "ready";
    flatGrayHierarchyPayload.integratedToneLayerJson["autoCandidateSelectedId"] = "base";
    flatGrayHierarchyPayload.integratedToneLayerJson["autoCandidateRenderedSolves"] = nlohmann::json::array({
        {
            { "id", "base" },
            { "success", true },
            { "renderScore", 0.58f },
            { "metrics", {
                { "meanLuma", 0.30f },
                { "medianLuma", 0.29f },
                { "p10Luma", 0.18f },
                { "p90Luma", 0.46f },
                { "shadowFraction", 0.14f },
                { "highlightFraction", 0.08f },
                { "clippedFraction", 0.001f },
                { "contrastSpan", 0.18f },
                { "meanRed", 0.31f },
                { "meanGreen", 0.30f },
                { "meanBlue", 0.29f },
                { "meanSaturation", 0.12f },
                { "lowSaturationFraction", 0.86f },
                { "highlightBandFraction", 0.28f },
                { "highlightMeanLuma", 0.56f },
                { "highlightLowSaturationFraction", 0.82f },
                { "highlightGrayRisk", 0.64f },
                { "highlightTileCoverage", 0.46f },
                { "highlightStructureScore", 0.38f },
                { "meaningfulHighlightPressure", 0.58f },
                { "edgeContrast", 0.18f },
                { "haloRiskFraction", 0.01f },
                { "shadowTextureRisk", 0.12f },
                { "localLumaSpread", 0.07f },
                { "localContrastPeak", 0.18f },
                { "localShadowPressure", 0.18f },
                { "localHighlightPressure", 0.12f },
                { "localDamageRiskMean", 0.08f },
                { "localDamageRiskPeak", 0.16f },
                { "localDamageRiskPeakTile", 4 },
                { "centerMeanLuma", 0.30f },
                { "centerShadowFraction", 0.10f },
                { "centerHighlightFraction", 0.04f }
            } }
        }
    });
    EditorModule::ApplyDevelopAutoSolve(flatGrayHierarchyPayload, metadata, true);
    const nlohmann::json flatGrayRegionEvidence =
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeRegionEvidence", nlohmann::json::object());
    const bool renderedHighlightGrayEvidenceWritten =
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightGrayRisk", 0.0f) > 0.50f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeMeaningfulHighlightPressure", 0.0f) > 0.48f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightBandFraction", 0.0f) > 0.20f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightMeanLuma", 1.0f) < 0.62f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightLowSaturationFraction", 0.0f) > 0.70f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightTileCoverage", 0.0f) > 0.40f &&
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightStructureScore", 0.0f) > 0.30f &&
        flatGrayRegionEvidence.value("meaningfulHighlightPressure", 0.0f) > 0.48f &&
        flatGrayRegionEvidence.value("highlightGrayRisk", 0.0f) > 0.50f;
    const nlohmann::json& flatGrayCandidateSolves =
        flatGrayHierarchyPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (flatGrayCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : flatGrayCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "naturalContrastGuard") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            naturalContrastGuardGenerated = true;
            naturalContrastGuardEligible =
                naturalContrastGuardEligible || eligible;
            naturalContrastGuardGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), flatGrayHierarchyPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            naturalContrastGuardHumanReadable =
                naturalContrastGuardHumanReadable ||
                (label.find("Natural") != std::string::npos &&
                 label.find("Contrast") != std::string::npos &&
                 reason.find("believable separation") != std::string::npos &&
                 reason.find("lighting hierarchy") != std::string::npos);
            naturalContrastGuardDiagnosticsWritten =
                naturalContrastGuardDiagnosticsWritten ||
                (dimensions.value("naturalContrastGuard", -1.0f) > 0.54f &&
                 dimensions.value("brightnessHierarchy", -1.0f) > 0.54f &&
                 dimensions.value("contrastShape", -1.0f) > 0.54f &&
                 changes.value("contrastBiasDelta", 0.0f) > 0.18f &&
                 changes.value("dynamicRangeDelta", 0.0f) < -0.06f &&
                 std::fabs(changes.value("brightnessIntentDelta", 0.0f)) < 0.04f);
        }
    }
    RenderGraphRawDevelopPayload flatGrayBaseCandidateRenderPayload;
    flatGrayBaseCandidateRenderPayload.settings = flatGrayHierarchyPayload.settings;
    flatGrayBaseCandidateRenderPayload.scenePrepEnabled = flatGrayHierarchyPayload.scenePrepEnabled;
    flatGrayBaseCandidateRenderPayload.scenePrepSettings = flatGrayHierarchyPayload.scenePrepSettings;
    flatGrayBaseCandidateRenderPayload.integratedToneEnabled = flatGrayHierarchyPayload.integratedToneEnabled;
    flatGrayBaseCandidateRenderPayload.integratedToneLayerJson = flatGrayHierarchyPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload naturalContrastGuardPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            flatGrayBaseCandidateRenderPayload,
            flatGrayHierarchyPayload.autoGuidance,
            naturalContrastGuardGuidance,
            "naturalContrastGuard",
            flatGrayHierarchyPayload.autoGuidance.intent);
    const bool naturalContrastGuardRenderPayloadConstrained =
        naturalContrastGuardGenerated &&
        naturalContrastGuardEligible &&
        naturalContrastGuardHumanReadable &&
        naturalContrastGuardDiagnosticsWritten &&
        std::abs(naturalContrastGuardPayload.settings.exposureStops -
            flatGrayBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(naturalContrastGuardPayload.scenePrepSettings.maxEvBias -
            flatGrayBaseCandidateRenderPayload.scenePrepSettings.maxEvBias) < 0.0001f &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "finishTone" &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", false) &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string()) == "naturalContrastGuard" &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) >
            flatGrayHierarchyPayload.autoGuidance.contrastBias + 0.18f &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f) >
            flatGrayHierarchyPayload.autoGuidance.highlightCharacter + 0.10f &&
        naturalContrastGuardPayload.integratedToneLayerJson.value("autoDynamicRange", 99.0f) <
            flatGrayHierarchyPayload.autoGuidance.dynamicRange - 0.06f;

    EditorNodeGraph::RawDevelopPayload underBrightHighlightPayload = balancedPayload;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.012f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.17f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.32f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.001f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.18f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.02f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.70f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 4.8f;
    underBrightHighlightPayload.integratedToneLayerJson["autoSceneProfile"] = 0;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.82f;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.08f;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.16f;
    underBrightHighlightPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    EditorModule::ApplyDevelopAutoSolve(underBrightHighlightPayload, metadata, true);
    const bool underBrightBroadHighlightsLifted =
        underBrightHighlightPayload.settings.exposureStops > balancedPayload.settings.exposureStops + 0.65f &&
        underBrightHighlightPayload.settings.exposureStops > specularPayload.settings.exposureStops + 0.30f &&
        underBrightHighlightPayload.scenePrepSettings.highlightProtectionBias < highlightHeavyPayload.scenePrepSettings.highlightProtectionBias - 0.30f;

    EditorNodeGraph::RawDevelopPayload readableShadowPayload = balancedPayload;
    readableShadowPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.018f;
    readableShadowPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.105f;
    readableShadowPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.68f;
    readableShadowPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.0f;
    readableShadowPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.18f;
    readableShadowPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.12f;
    readableShadowPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.76f;
    readableShadowPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 3.6f;
    readableShadowPayload.integratedToneLayerJson["autoSceneProfile"] = 2;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.46f;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.16f;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.46f;
    readableShadowPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.04f;
    EditorModule::ApplyDevelopAutoSolve(readableShadowPayload, metadata, true);
    const nlohmann::json& readableShadowCandidateSolves =
        readableShadowPayload.integratedToneLayerJson.value("autoCandidateSolves", nlohmann::json::array());
    if (readableShadowCandidateSolves.is_array()) {
        for (const nlohmann::json& candidate : readableShadowCandidateSolves) {
            if (!candidate.is_object() ||
                candidate.value("id", std::string()) != "shadowReadabilityLift") {
                continue;
            }
            const std::string status = candidate.value("status", std::string());
            const bool eligible = status == "selected" || status == "survivor";
            shadowReadabilityLiftGenerated = true;
            shadowReadabilityLiftEligible =
                shadowReadabilityLiftEligible || eligible;
            shadowReadabilityLiftGuidance =
                guidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), readableShadowPayload.autoGuidance);
            const std::string label = candidate.value("label", std::string());
            const std::string reason = candidate.value("reason", std::string());
            const nlohmann::json scoreComponents =
                candidate.value("scoreComponents", nlohmann::json::object());
            const nlohmann::json dimensions =
                scoreComponents.value("dimensions", nlohmann::json::object());
            const nlohmann::json changes =
                candidate.value("changes", nlohmann::json::object());
            shadowReadabilityLiftHumanReadable =
                shadowReadabilityLiftHumanReadable ||
                (label.find("Shadow") != std::string::npos &&
                 label.find("Readability") != std::string::npos &&
                 reason.find("readable shadows") != std::string::npos &&
                 reason.find("RAW placement stable") != std::string::npos);
            shadowReadabilityLiftDiagnosticsWritten =
                shadowReadabilityLiftDiagnosticsWritten ||
                (dimensions.value("shadowReadabilityLift", -1.0f) > 0.54f &&
                 dimensions.value("midtonePlacement", -1.0f) > 0.46f &&
                 changes.value("shadowLiftDelta", 0.0f) > 0.20f &&
                 changes.value("dynamicRangeDelta", 0.0f) > 0.14f);
        }
    }
    RenderGraphRawDevelopPayload readableShadowBaseCandidateRenderPayload;
    readableShadowBaseCandidateRenderPayload.settings = readableShadowPayload.settings;
    readableShadowBaseCandidateRenderPayload.scenePrepEnabled = readableShadowPayload.scenePrepEnabled;
    readableShadowBaseCandidateRenderPayload.scenePrepSettings = readableShadowPayload.scenePrepSettings;
    readableShadowBaseCandidateRenderPayload.integratedToneEnabled = readableShadowPayload.integratedToneEnabled;
    readableShadowBaseCandidateRenderPayload.integratedToneLayerJson = readableShadowPayload.integratedToneLayerJson;
    const RenderGraphRawDevelopPayload shadowReadabilityLiftPayload =
        EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
            readableShadowBaseCandidateRenderPayload,
            readableShadowPayload.autoGuidance,
            shadowReadabilityLiftGuidance,
            "shadowReadabilityLift",
            readableShadowPayload.autoGuidance.intent);
    const bool shadowReadabilityLiftRenderPayloadConstrained =
        shadowReadabilityLiftGenerated &&
        shadowReadabilityLiftEligible &&
        shadowReadabilityLiftHumanReadable &&
        shadowReadabilityLiftDiagnosticsWritten &&
        std::abs(shadowReadabilityLiftPayload.settings.exposureStops -
            readableShadowBaseCandidateRenderPayload.settings.exposureStops) < 0.0001f &&
        std::abs(shadowReadabilityLiftPayload.settings.highlightStrength -
            readableShadowBaseCandidateRenderPayload.settings.highlightStrength) < 0.0001f &&
        shadowReadabilityLiftPayload.scenePrepSettings.maxEvBias >=
            readableShadowBaseCandidateRenderPayload.scenePrepSettings.maxEvBias + 0.08f &&
        shadowReadabilityLiftPayload.scenePrepSettings.noiseProtectionBias >=
            readableShadowBaseCandidateRenderPayload.scenePrepSettings.noiseProtectionBias - 0.0001f &&
        shadowReadabilityLiftPayload.scenePrepSettings.shadowLiftLimitBias <=
            readableShadowBaseCandidateRenderPayload.scenePrepSettings.shadowLiftLimitBias + 0.0001f &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string()) == "scenePrep" &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraintApplied", false) &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenRaw", false) &&
        !shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraintFrozenScenePrep", true) &&
        shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string()) == "shadowReadabilityLift";

    EditorNodeGraph::RawDevelopPayload darkScenePayload = balancedPayload;
    darkScenePayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.006f;
    darkScenePayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.045f;
    darkScenePayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.58f;
    darkScenePayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.0f;
    darkScenePayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.20f;
    darkScenePayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.08f;
    darkScenePayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.74f;
    darkScenePayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.2f;
    darkScenePayload.integratedToneLayerJson["autoSceneProfile"] = 2;
    darkScenePayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 1.02f;
    darkScenePayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.02f;
    darkScenePayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.32f;
    darkScenePayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.00f;
    EditorModule::ApplyDevelopAutoSolve(darkScenePayload, metadata, true);
    const bool darkSceneLifted =
        darkScenePayload.settings.exposureStops > balancedPayload.settings.exposureStops + 1.00f &&
        darkScenePayload.scenePrepSettings.maxEvBias > 1.00f &&
        darkScenePayload.scenePrepSettings.strength >= balancedPayload.scenePrepSettings.strength - 0.02f;
    const bool rawBaselineCarriesDarkLift =
        darkScenePayload.settings.exposureStops > balancedPayload.settings.exposureStops + 1.00f &&
        darkScenePayload.scenePrepSettings.strength <= scenePrepBefore.strength + 0.25f;
    const float darkRawExposureLift = darkScenePayload.settings.exposureStops - balancedPayload.settings.exposureStops;
    const float darkScenePrepAmountLift = darkScenePayload.scenePrepSettings.strength - balancedPayload.scenePrepSettings.strength;
    const bool rawBaselineDominatesDarkLift =
        darkRawExposureLift > 1.00f &&
        darkScenePrepAmountLift < 0.25f &&
        darkRawExposureLift > darkScenePrepAmountLift * 4.0f;

    EditorNodeGraph::RawDevelopPayload noisyLowLightPayload = balancedPayload;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneShadowPercentile"] = 0.01f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneMidtonePercentile"] = 0.09f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneHighlightPercentile"] = 0.62f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneClippingRatio"] = 0.000f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneNoiseRisk"] = 0.88f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneHighlightPressure"] = 0.16f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.30f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = 2.6f;
    noisyLowLightPayload.integratedToneLayerJson["autoSceneProfile"] = 4;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedBaseEv"] = 0.92f;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 0.96f;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.06f;
    noisyLowLightPayload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    EditorModule::ApplyDevelopAutoSolve(noisyLowLightPayload, metadata, true);
    const bool noisyProfilePrefersProtection =
        noisyLowLightPayload.settings.mosaicDenoise.enabled &&
        noisyLowLightPayload.settings.mosaicDenoise.radius >= 3 &&
        noisyLowLightPayload.settings.mosaicDenoise.iterations >= 2 &&
        noisyLowLightPayload.settings.mosaicDenoise.lumaStrength > balancedPayload.settings.mosaicDenoise.lumaStrength + 0.10f &&
        noisyLowLightPayload.scenePrepSettings.noiseProtectionBias > balancedPayload.scenePrepSettings.noiseProtectionBias + 0.18f &&
        noisyLowLightPayload.scenePrepSettings.shadowLiftLimit > balancedPayload.scenePrepSettings.shadowLiftLimit + 0.05f;

    EditorNodeGraph::RawDevelopPayload flatIntentPayload = balancedPayload;
    flatIntentPayload.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    EditorModule::ApplyDevelopAutoSolve(flatIntentPayload, metadata, true);
    EditorNodeGraph::RawDevelopPayload punchyIntentPayload = balancedPayload;
    punchyIntentPayload.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    EditorModule::ApplyDevelopAutoSolve(punchyIntentPayload, metadata, true);
    const bool modeIntentAffectsSolve =
        flatIntentPayload.scenePrepSettings.maxEvBias > balancedPayload.scenePrepSettings.maxEvBias + 0.10f &&
        flatIntentPayload.scenePrepSettings.highlightProtectionBias > balancedPayload.scenePrepSettings.highlightProtectionBias + 0.05f &&
        flatIntentPayload.integratedToneLayerJson.value("autoContrastBias", 99.0f) <
            balancedPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f) - 0.20f &&
        punchyIntentPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f) >
            balancedPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f) + 0.20f;
    const bool modeIntentForwarded =
        flatIntentPayload.integratedToneLayerJson.value("autoIntent", std::string()) == "FlatEditingBase" &&
        punchyIntentPayload.integratedToneLayerJson.value("autoIntent", std::string()) == "PunchyHighContrast";
    const bool success =
        defaultIntentIsNatural &&
        exposureAuthored &&
        highlightAuthored &&
        cleanupAuthored &&
        scenePrepAuthored &&
        finishQueued &&
        finishGuidanceForwarded &&
        rejectedCandidateMemoryRecorded &&
        rejectedCandidateMemorySuppressesRepeat &&
        renderedRejectionMemorySuppressesRepeat &&
        selectedCandidateScoreComponentsWritten &&
        stagedAutoSolveDiagnosticsWritten &&
        candidateLearningRecordedNotApplied &&
        renderedFeedbackLoopAwaitingMetrics &&
        renderedContinuationPolicyAwaitingMetrics &&
        convergenceEvidenceAwaitingMetrics &&
        brightnessExposureTelemetryForwarded &&
        exposureDiagnosticsForwarded &&
        renderedVisualRiskMetricsPopulated &&
        renderedHighlightGrayMetricsPopulated &&
        renderedMeaningfulHighlightMetricsPopulated &&
        renderedLocalMetricsPopulated &&
        renderedSubjectMetricsPopulated &&
        renderedMarkedSubjectMetricsPopulated &&
        renderedMarkedLowPriorityMetricsPopulated &&
        renderedSpatialRiskMetricsPopulated &&
        renderedColorCastMetricsPopulated &&
        renderedDuplicateMetricDistanceWorks &&
        renderedStageBoundaryClassifierWorks &&
        renderedStageAwareDuplicateClusteringWorks &&
        renderedDamageClassifierWorks &&
        renderedRelativeComparisonWorks &&
        modeNeighborCandidateGenerated &&
        modeNeighborCandidateEligible &&
        modeNeighborCandidateHumanReadable &&
        modeNeighborCandidateMeaningfullyDifferent &&
        renderedLocalRefineIntentWorks &&
        renderedRefineIntentRelevanceWorks &&
        renderedFeedbackAdoptedCandidate &&
        renderedFeedbackLoopActive &&
        renderedContinuationPolicyContinues &&
        convergenceEvidenceContinuesAfterFeedback &&
        convergenceEvidenceAdmissionTightensMarginalContinuation &&
        continuationBiasDiagnosticsWritten &&
        continuationBiasBoostsFinishToneCandidates &&
        continuationExpansionDiagnosticsWritten &&
        continuationExpansionAddsFinishToneFamily &&
        renderedFeedbackRevisionStageWritten &&
        renderedFeedbackFinishToneStageOverride &&
        renderedFeedbackMergedCandidate &&
        renderedFeedbackPairMergedCandidate &&
        renderedFeedbackEnsembleMergedCandidate &&
        renderedFeedbackCandidateCarriedForward &&
        renderedSurvivorCandidateCarriedForward &&
        renderedSurvivorCarryForwardCountWritten &&
        renderedFeedbackRefinedCandidate &&
        renderedRefineRevisionStageIsScenePrep &&
        renderedRefineStagePlanTargetsScenePrep &&
        renderedCleanupRefinedCandidate &&
        renderedCleanupRevisionStageIsRawCleanup &&
        renderedCleanupStagePlanTargetsRawBase &&
        renderedRepeatedRefineStops &&
        renderedMonotonicShadowRiskStops &&
        renderedNoImprovementStops &&
        renderedStableMetricsConverge &&
        renderedStableLoopConverged &&
        renderedContinuationPolicyStopsStable &&
        convergenceEvidenceStopsStable &&
        renderedTrendConverges &&
        renderedTrendLoopConverged &&
        renderedContinuationPolicyStopsTrend &&
        convergenceEvidenceStopsTrend &&
        renderedContinuationPolicyStopsAtPassLimit &&
        convergenceEvidenceStopsAtPassLimit &&
        renderedFeedbackStopConvergenceClassifierWorks &&
        renderedNoBestStopsWithoutConverging &&
        convergenceEvidenceStopsNoBest &&
        cleanupTextureCandidateRenderPayloadsDiverge &&
        highlightProtectedMidsGenerated &&
        highlightProtectedMidsEligible &&
        highlightProtectedMidsMeaningfullyDifferent &&
        highlightProtectedMidsRenderPayloadDiverges &&
        finishToneProbeGenerated &&
        finishToneProbeEligible &&
        finishToneProbeHumanReadable &&
        finishToneProbeMeaningfullyDifferent &&
        finishToneProbeRenderPayloadConstrained &&
        dynamicRangeStrategyDiagnosticsWritten &&
        dynamicRangeStrategyMapDiagnosticsWritten &&
        localExposureStrategyDiagnosticsWritten &&
        localExposureStrategyAuthoredScenePrep &&
        localExposureStrategyCandidatePayloadCarried &&
        broadHighlightGuardGenerated &&
        broadHighlightGuardEligible &&
        broadHighlightGuardHumanReadable &&
        broadHighlightGuardDiagnosticsWritten &&
        broadHighlightGuardRenderPayloadConstrained &&
        naturalContrastGuardGenerated &&
        naturalContrastGuardEligible &&
        naturalContrastGuardHumanReadable &&
        naturalContrastGuardDiagnosticsWritten &&
        naturalContrastGuardRenderPayloadConstrained &&
        brightHighlightRolloffGenerated &&
        brightHighlightRolloffEligible &&
        brightHighlightRolloffHumanReadable &&
        brightHighlightRolloffDiagnosticsWritten &&
        brightHighlightRolloffRenderPayloadConstrained &&
        luminousHighlightAnchorGenerated &&
        luminousHighlightAnchorEligible &&
        luminousHighlightAnchorHumanReadable &&
        luminousHighlightAnchorDiagnosticsWritten &&
        luminousHighlightAnchorRenderPayloadConstrained &&
        renderedHighlightGrayEvidenceWritten &&
        specularHighlightToleranceGenerated &&
        specularHighlightToleranceEligible &&
        specularHighlightToleranceHumanReadable &&
        specularHighlightToleranceDiagnosticsWritten &&
        specularHighlightToleranceRenderPayloadConstrained &&
        regionalEvidenceDiagnosticsWritten &&
        subjectSceneIntentDiagnosticsWritten &&
        subjectSceneIntentScoreComponentsWritten &&
        subjectSceneIntentBiasesScoring &&
        userSubjectSceneIntentDiagnosticsWritten &&
        userSubjectSceneIntentScoreComponentsWritten &&
        subjectImportanceGuidanceDiagnosticsWritten &&
        subjectBrushGuidanceDiagnosticsWritten &&
        subjectBrushDisabledIgnored &&
        subjectBrushReduceGuidanceDiagnosticsWritten &&
        subjectReadableMidsGenerated &&
        subjectReadableMidsEligible &&
        subjectReadableMidsHumanReadable &&
        subjectReadableMidsDiagnosticsWritten &&
        subjectReadableMidsRenderPayloadConstrained &&
        sceneMoodPreservationGenerated &&
        sceneMoodPreservationEligible &&
        sceneMoodPreservationHumanReadable &&
        sceneMoodPreservationDiagnosticsWritten &&
        sceneMoodPreservationRenderPayloadConstrained &&
        haloSafeLocalRangeGenerated &&
        haloSafeLocalRangeEligible &&
        haloSafeLocalRangeHumanReadable &&
        haloSafeLocalRangeDiagnosticsWritten &&
        haloSafeLocalRangeRenderPayloadConstrained &&
        localRangeGuardGenerated &&
        localRangeGuardEligible &&
        localRangeGuardDiagnosticsWritten &&
        localRangeGuardRenderPayloadConstrained &&
        shadowReadabilityLiftGenerated &&
        shadowReadabilityLiftEligible &&
        shadowReadabilityLiftHumanReadable &&
        shadowReadabilityLiftDiagnosticsWritten &&
        shadowReadabilityLiftRenderPayloadConstrained &&
        shadowNoiseFloorGenerated &&
        shadowNoiseFloorEligible &&
        shadowNoiseFloorDiagnosticsWritten &&
        shadowNoiseFloorRenderPayloadConstrained &&
        whiteBalanceProbeGenerated &&
        whiteBalanceProbeEligible &&
        whiteBalanceProbeHumanReadable &&
        whiteBalanceProbeDiagnosticsWritten &&
        whiteBalanceProbeRenderPayloadDiverges &&
        scenePrepCandidateStageConstrained &&
        finishToneCandidateStageConstrained &&
        renderedStageRelevanceWorks &&
        renderedRefineIntentRelevanceWorks &&
        renderedStageCacheValidationWorks &&
        renderedStageSchedulerClassificationWorks &&
        developCandidateRenderBudgetAllowsMultiNodeCoverage &&
        developCandidateMetricReadbackBudgetWorks &&
        rawDevelopStageCacheMemoryPolicyWorks &&
        developAdaptiveRenderBudgetWorks &&
        developCandidateFeedbackGateDropsStale &&
        developCandidateFeedbackGateDefersRecent &&
        developCandidateFeedbackGateAppliesAfterQuiet &&
        developCandidateRenderAdmissionDefersRecent &&
        developCandidateFeedbackQuietRemainingWorks &&
        developStaleSnapshotAbortWorks &&
        developCandidateProgressLabelWorks &&
        partialSolvePreservedRawAuthorship &&
        partialSolveUpdatedPrepAndFinish &&
        repeatedFullSolveStable &&
        positiveExposureBiasPreserved &&
        highlightProfilePrefersHeadroom &&
        tinySpecularsDoNotDragExposure &&
        broadHighlightsProtectedMoreThanSpeculars &&
        underBrightBroadHighlightsLifted &&
        darkSceneLifted &&
        rawBaselineCarriesDarkLift &&
        rawBaselineDominatesDarkLift &&
        noisyProfilePrefersProtection &&
        modeIntentAffectsSolve &&
        modeIntentForwarded;

    if (!success) {
        std::cerr
            << "Develop auto solve validation failed:"
            << " defaultIntentIsNatural=" << defaultIntentIsNatural
            << " exposureAuthored=" << exposureAuthored
            << " highlightAuthored=" << highlightAuthored
            << " cleanupAuthored=" << cleanupAuthored
            << " scenePrepAuthored=" << scenePrepAuthored
            << " finishQueued=" << finishQueued
            << " finishGuidanceForwarded=" << finishGuidanceForwarded
            << " requestedGuidanceForwarded=" << requestedGuidanceForwarded
            << " selectedCandidateGuidanceForwarded=" << selectedCandidateGuidanceForwarded
            << " candidateDiagnosticsWritten=" << candidateDiagnosticsWritten
            << " selectedCandidateScoreComponentsWritten=" << selectedCandidateScoreComponentsWritten
            << " renderedFeedbackLoopAwaitingMetrics=" << renderedFeedbackLoopAwaitingMetrics
            << " initialRenderedLoopState=" << initialRenderedLoop.value("state", std::string())
            << " initialRenderedLoopNextStep=" << initialRenderedLoop.value("nextStep", std::string())
            << " renderedContinuationPolicyAwaitingMetrics=" << renderedContinuationPolicyAwaitingMetrics
            << " initialContinuationDecision=" << initialContinuationPolicy.value("decision", std::string())
            << " initialContinuationNextStep=" << initialContinuationPolicy.value("nextStep", std::string())
            << " convergenceEvidenceAwaitingMetrics=" << convergenceEvidenceAwaitingMetrics
            << " initialConvergenceState=" << initialConvergenceEvidence.value("state", std::string())
            << " initialConvergenceDecision=" << initialConvergenceEvidence.value("decision", std::string())
            << " initialConvergenceReason=" << initialConvergenceEvidence.value("reason", std::string())
            << " stagedAutoSolveDiagnosticsWritten=" << stagedAutoSolveDiagnosticsWritten
            << " autoStageSolveVersion=" << payload.integratedToneLayerJson.value("autoStageSolveVersion", std::string())
            << " autoStageEarliestDirtyStage=" << payload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string())
            << " autoStageValidationState=" << payload.integratedToneLayerJson.value("autoStageValidationState", std::string())
            << " autoStageStageCount=" << (stageSolves.is_array() ? stageSolves.size() : 0)
            << " autoStageRawBaseFingerprint=" << stageFingerprints.value("rawBase", static_cast<std::uint64_t>(0))
            << " autoStageScenePrepFingerprint=" << stageFingerprints.value("scenePrep", static_cast<std::uint64_t>(0))
            << " autoStageFinishToneFingerprint=" << stageFingerprints.value("finishTone", static_cast<std::uint64_t>(0))
            << " autoCandidateScoreVersion=" << payload.integratedToneLayerJson.value("autoCandidateScoreVersion", std::string())
            << " candidateLearningRecordedNotApplied=" << candidateLearningRecordedNotApplied
            << " candidateLearningVersion=" << payload.integratedToneLayerJson.value("autoCandidateLearningVersion", std::string())
            << " candidateLearningStatus=" << payload.integratedToneLayerJson.value("autoCandidateLearningStatus", std::string())
            << " candidateLearningEventCount=" << payload.integratedToneLayerJson.value("autoCandidateLearningEventCount", -1)
            << " rejectedCandidateMemoryRecorded=" << rejectedCandidateMemoryRecorded
            << " rejectedCandidateMemorySuppressesRepeat=" << rejectedCandidateMemorySuppressesRepeat
            << " repeatedCandidateRejectedFromMemory=" << repeatedCandidateRejectedFromMemory
            << " renderedRejectionMemorySuppressesRepeat=" << renderedRejectionMemorySuppressesRepeat
            << " renderedMemoryCandidateId=" << renderedMemoryCandidateId
            << " renderedMemoryCandidateFingerprint=" << renderedMemoryCandidateFingerprint
            << " renderedRejectedMemorySuppressionCount=" << renderedRejectionMemoryPayload.integratedToneLayerJson.value("autoCandidateRenderedRejectedMemorySuppressionCount", -1)
            << " rejectedMemoryInitialSize=" << (candidateRejectedMemory.is_array() ? candidateRejectedMemory.size() : 0)
            << " rejectedMemorySuppressionCount=" << rejectedMemoryPayload.integratedToneLayerJson.value("autoCandidateRejectedMemorySuppressionCount", -1)
            << " candidateSolveCanBiasAuthoredGuidance=" << candidateSolveCanBiasAuthoredGuidance
            << " cleanShadowCandidateGenerated=" << cleanShadowCandidateGenerated
            << " preserveTextureCandidateGenerated=" << preserveTextureCandidateGenerated
            << " cleanupTextureCandidateRenderPayloadsDiverge=" << cleanupTextureCandidateRenderPayloadsDiverge
            << " highlightProtectedMidsGenerated=" << highlightProtectedMidsGenerated
            << " highlightProtectedMidsEligible=" << highlightProtectedMidsEligible
            << " highlightProtectedMidsMeaningfullyDifferent=" << highlightProtectedMidsMeaningfullyDifferent
            << " highlightProtectedMidsRenderPayloadDiverges=" << highlightProtectedMidsRenderPayloadDiverges
            << " protectedMidsExposure=" << protectedMidsPayload.settings.exposureStops
            << " protectedMidsMaxEvBias=" << protectedMidsPayload.scenePrepSettings.maxEvBias
            << " protectedMidsHighlightBias=" << protectedMidsPayload.scenePrepSettings.highlightProtectionBias
            << " protectedMidsDynamicRange=" << protectedMidsPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f)
            << " protectedMidsShadowBias=" << protectedMidsPayload.integratedToneLayerJson.value("autoShadowBias", -99.0f)
            << " protectedMidsToneHighlightBias=" << protectedMidsPayload.integratedToneLayerJson.value("autoHighlightBias", -99.0f)
            << " finishToneProbeGenerated=" << finishToneProbeGenerated
            << " finishToneProbeEligible=" << finishToneProbeEligible
            << " finishToneProbeHumanReadable=" << finishToneProbeHumanReadable
            << " finishToneProbeMeaningfullyDifferent=" << finishToneProbeMeaningfullyDifferent
            << " finishToneProbeRenderPayloadConstrained=" << finishToneProbeRenderPayloadConstrained
            << " finishToneProbeId=" << finishToneProbeRenderId
            << " finishToneProbeStageConstraint=" << finishToneProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " finishToneProbeDiagnosticId=" << finishToneProbePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " finishToneProbeContrastBias=" << finishToneProbePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " finishToneProbeHighlightCharacter=" << finishToneProbePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " dynamicRangeStrategyDiagnosticsWritten=" << dynamicRangeStrategyDiagnosticsWritten
            << " dynamicRangeStrategyMapDiagnosticsWritten=" << dynamicRangeStrategyMapDiagnosticsWritten
            << " dynamicRangeStrategyMapVersion=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVersion", std::string())
            << " dynamicRangeStrategyMapHighlightShadowAxis=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightShadowAxis", -99.0f)
            << " dynamicRangeStrategyMapContrastRangeAxis=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapContrastRangeAxis", -99.0f)
            << " dynamicRangeStrategyMapHighlightPriority=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapHighlightPriority", -1.0f)
            << " dynamicRangeStrategyMapShadowVisibility=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapShadowVisibility", -1.0f)
            << " dynamicRangeStrategyMapNaturalContrast=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapNaturalContrast", -1.0f)
            << " dynamicRangeStrategyMapVisibleRange=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyMapVisibleRange", -1.0f)
            << " dynamicRangeStrategyMapScoreComponentsWritten=" << dynamicRangeStrategyMapScoreComponentsWritten
            << " localExposureStrategyDiagnosticsWritten=" << localExposureStrategyDiagnosticsWritten
            << " localExposureStrategyAuthoredScenePrep=" << localExposureStrategyAuthoredScenePrep
            << " localExposureStrategyCandidatePayloadCarried=" << localExposureStrategyCandidatePayloadCarried
            << " localExposureStrategyVersion=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyVersion", std::string())
            << " localExposureStrategyId=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureStrategyId", std::string())
            << " localExposureRangeRedistribution=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureRangeRedistribution", -1.0f)
            << " localExposureHighlightCompression=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCompression", -1.0f)
            << " localExposureShadowOpening=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowOpening", -1.0f)
            << " localExposureNoiseGuard=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureNoiseGuard", -1.0f)
            << " localExposureHaloGuard=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloGuard", -1.0f)
            << " localExposureHighlightCrowding=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHighlightCrowding", -1.0f)
            << " localExposureShadowCrowding=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureShadowCrowding", -1.0f)
            << " localExposureHaloStress=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureHaloStress", -1.0f)
            << " localExposureFlatnessRisk=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureFlatnessRisk", -1.0f)
            << " localExposureDamageRisk=" << payload.integratedToneLayerJson.value("autoDynamicRangeLocalExposureDamageRisk", -1.0f)
            << " authoredLocalExposureStrategyId=" << payload.integratedToneLayerJson.value("autoAuthoredLocalExposureStrategyId", std::string())
            << " candidateLocalExposureStrategyId=" << localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateLocalExposureStrategyId", std::string())
            << " dynamicRangeStrategyId=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyId", std::string())
            << " dynamicRangeStrategyLabel=" << payload.integratedToneLayerJson.value("autoDynamicRangeStrategyLabel", std::string())
            << " dynamicRangeHighlightImportance=" << payload.integratedToneLayerJson.value("autoDynamicRangeHighlightImportance", -1.0f)
            << " dynamicRangeShadowReadability=" << payload.integratedToneLayerJson.value("autoDynamicRangeShadowReadability", -1.0f)
            << " dynamicRangeBrightHighlightRolloffNeed=" << payload.integratedToneLayerJson.value("autoDynamicRangeBrightHighlightRolloffNeed", -1.0f)
            << " dynamicRangeHighlightBrightnessAnchorNeed=" << payload.integratedToneLayerJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", -1.0f)
            << " broadHighlightGuardGenerated=" << broadHighlightGuardGenerated
            << " broadHighlightGuardEligible=" << broadHighlightGuardEligible
            << " broadHighlightGuardHumanReadable=" << broadHighlightGuardHumanReadable
            << " broadHighlightGuardDiagnosticsWritten=" << broadHighlightGuardDiagnosticsWritten
            << " broadHighlightGuardRenderPayloadConstrained=" << broadHighlightGuardRenderPayloadConstrained
            << " broadHighlightGuardNeed=" << highlightHeavyPayload.integratedToneLayerJson.value("autoDynamicRangeBroadHighlightGuardNeed", -1.0f)
            << " broadHighlightGuardStageConstraint=" << broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " broadHighlightGuardScenePrepProbe=" << broadHighlightGuardPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " broadHighlightGuardMinEvBias=" << broadHighlightGuardPayload.scenePrepSettings.minEvBias
            << " broadHighlightGuardHighlightBias=" << broadHighlightGuardPayload.scenePrepSettings.highlightProtectionBias
            << " naturalContrastGuardGenerated=" << naturalContrastGuardGenerated
            << " naturalContrastGuardEligible=" << naturalContrastGuardEligible
            << " naturalContrastGuardHumanReadable=" << naturalContrastGuardHumanReadable
            << " naturalContrastGuardDiagnosticsWritten=" << naturalContrastGuardDiagnosticsWritten
            << " naturalContrastGuardRenderPayloadConstrained=" << naturalContrastGuardRenderPayloadConstrained
            << " naturalContrastGuardNeed=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeNaturalContrastGuardNeed", -1.0f)
            << " naturalContrastGuardStageConstraint=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " naturalContrastGuardDiagnosticId=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " naturalContrastGuardDynamicRange=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f)
            << " naturalContrastGuardContrastBias=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " naturalContrastGuardHighlightCharacter=" << naturalContrastGuardPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " brightHighlightRolloffGenerated=" << brightHighlightRolloffGenerated
            << " brightHighlightRolloffEligible=" << brightHighlightRolloffEligible
            << " brightHighlightRolloffHumanReadable=" << brightHighlightRolloffHumanReadable
            << " brightHighlightRolloffDiagnosticsWritten=" << brightHighlightRolloffDiagnosticsWritten
            << " brightHighlightRolloffRenderPayloadConstrained=" << brightHighlightRolloffRenderPayloadConstrained
            << " brightHighlightRolloffStageConstraint=" << brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " brightHighlightRolloffDiagnosticId=" << brightHighlightRolloffPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " brightHighlightRolloffHighlightCharacter=" << brightHighlightRolloffPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " luminousHighlightAnchorGenerated=" << luminousHighlightAnchorGenerated
            << " luminousHighlightAnchorEligible=" << luminousHighlightAnchorEligible
            << " luminousHighlightAnchorHumanReadable=" << luminousHighlightAnchorHumanReadable
            << " luminousHighlightAnchorDiagnosticsWritten=" << luminousHighlightAnchorDiagnosticsWritten
            << " luminousHighlightAnchorRenderPayloadConstrained=" << luminousHighlightAnchorRenderPayloadConstrained
            << " luminousHighlightAnchorNeed=" << payload.integratedToneLayerJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", -1.0f)
            << " luminousHighlightAnchorStageConstraint=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " luminousHighlightAnchorDiagnosticId=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " luminousHighlightAnchorDynamicRange=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoDynamicRange", -99.0f)
            << " luminousHighlightAnchorContrastBias=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " luminousHighlightAnchorHighlightCharacter=" << luminousHighlightAnchorPayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " renderedHighlightGrayEvidenceWritten=" << renderedHighlightGrayEvidenceWritten
            << " flatGrayHighlightGrayRisk=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightGrayRisk", -1.0f)
            << " flatGrayMeaningfulHighlightPressure=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeMeaningfulHighlightPressure", -1.0f)
            << " flatGrayHighlightBandFraction=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightBandFraction", -1.0f)
            << " flatGrayHighlightMeanLuma=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightMeanLuma", -1.0f)
            << " flatGrayHighlightLowSat=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightLowSaturationFraction", -1.0f)
            << " flatGrayHighlightTileCoverage=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightTileCoverage", -1.0f)
            << " flatGrayHighlightStructureScore=" << flatGrayHierarchyPayload.integratedToneLayerJson.value("autoDynamicRangeHighlightStructureScore", -1.0f)
            << " specularHighlightToleranceGenerated=" << specularHighlightToleranceGenerated
            << " specularHighlightToleranceEligible=" << specularHighlightToleranceEligible
            << " specularHighlightToleranceHumanReadable=" << specularHighlightToleranceHumanReadable
            << " specularHighlightToleranceDiagnosticsWritten=" << specularHighlightToleranceDiagnosticsWritten
            << " specularHighlightToleranceRenderPayloadConstrained=" << specularHighlightToleranceRenderPayloadConstrained
            << " specularHighlightToleranceNeed=" << specularPayload.integratedToneLayerJson.value("autoDynamicRangeSpecularHighlightToleranceNeed", -1.0f)
            << " specularHighlightToleranceStageConstraint=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " specularHighlightToleranceDiagnosticId=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoCandidateFinishToneProbe", std::string())
            << " specularHighlightToleranceHighlightCharacter=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightCharacter", -99.0f)
            << " specularHighlightToleranceHighlightBias=" << specularHighlightTolerancePayload.integratedToneLayerJson.value("autoHighlightBias", -99.0f)
            << " regionalEvidenceDiagnosticsWritten=" << regionalEvidenceDiagnosticsWritten
            << " subjectSceneIntentDiagnosticsWritten=" << subjectSceneIntentDiagnosticsWritten
            << " subjectSceneIntentScoreComponentsWritten=" << subjectSceneIntentScoreComponentsWritten
            << " subjectSceneIntentBiasesScoring=" << subjectSceneIntentBiasesScoring
            << " subjectSceneIntentVersion=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneIntentVersion", std::string())
            << " subjectSceneIntentId=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneIntentId", std::string())
            << " subjectSceneIntentLabel=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneIntentLabel", std::string())
            << " subjectSceneBrushStatus=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectSceneAutomaticConfidence=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneAutomaticConfidence", -1.0f)
            << " subjectSceneCenterPrior=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneCenterPrior", -1.0f)
            << " subjectSceneReadabilityPressure=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneReadabilityPressure", -1.0f)
            << " subjectSceneProtectionPressure=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneProtectionPressure", -1.0f)
            << " subjectSceneMoodPreservationPressure=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneMoodPreservationPressure", -1.0f)
            << " subjectSceneSubjectSceneAxis=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneSubjectSceneAxis", -99.0f)
            << " subjectSceneMoodReadabilityAxis=" << regionalEvidencePayload.integratedToneLayerJson.value("autoSubjectSceneMoodReadabilityAxis", -99.0f)
            << " userSubjectSceneIntentDiagnosticsWritten=" << userSubjectSceneIntentDiagnosticsWritten
            << " userSubjectSceneIntentScoreComponentsWritten=" << userSubjectSceneIntentScoreComponentsWritten
            << " userSubjectSceneGuidanceStatus=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " userSubjectSceneGuidanceStrength=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStrength", -1.0f)
            << " userSubjectSceneBias=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserSubjectSceneBias", -99.0f)
            << " userMoodReadabilityBias=" << userSubjectIntentPayload.integratedToneLayerJson.value("autoSubjectSceneUserMoodReadabilityBias", -99.0f)
            << " subjectImportanceGuidanceDiagnosticsWritten=" << subjectImportanceGuidanceDiagnosticsWritten
            << " subjectImportanceGuidanceStatus=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " subjectImportanceBrushStatus=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectImportanceRegionCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceRegionCount", -1)
            << " subjectImportanceStrokeCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrokeCount", -1)
            << " subjectImportanceRequestedRegionCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoRequestedSubjectImportanceRegionCount", -1)
            << " subjectImportanceRequestedStrokeCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoRequestedSubjectImportanceStrokeCount", -1)
            << " subjectImportanceStrength=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrength", -1.0f)
            << " subjectImportanceReveal=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceReveal", -1.0f)
            << " subjectImportanceSolveNoteVersion=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneSolveNotesVersion", std::string())
            << " subjectImportanceSolveNoteCount=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectSceneSolveNoteCount", -1)
            << " subjectImportancePrimarySolveNote=" << subjectImportancePayload.integratedToneLayerJson.value("autoSubjectScenePrimarySolveNote", std::string())
            << " subjectImportanceFingerprintA=" << subjectImportanceFingerprintA
            << " subjectImportanceFingerprintB=" << subjectImportanceFingerprintB
            << " subjectImportanceVisualFingerprint=" << subjectImportanceVisualFingerprint
            << " subjectImportanceScoreComponentsWritten=" << subjectImportanceScoreComponentsWritten
            << " subjectBrushGuidanceDiagnosticsWritten=" << subjectBrushGuidanceDiagnosticsWritten
            << " subjectBrushGuidanceStatus=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " subjectBrushStatus=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectBrushRegionCount=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceRegionCount", -1)
            << " subjectBrushStrokeCount=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrokeCount", -1)
            << " subjectBrushRequestedStrokeCount=" << subjectBrushPayload.integratedToneLayerJson.value("autoRequestedSubjectImportanceStrokeCount", -1)
            << " subjectBrushReveal=" << subjectBrushPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceReveal", -1.0f)
            << " subjectBrushFingerprintA=" << subjectBrushFingerprintA
            << " subjectBrushFingerprintB=" << subjectBrushFingerprintB
            << " subjectBrushScoreComponentsWritten=" << subjectBrushScoreComponentsWritten
            << " subjectBrushDisabledIgnored=" << subjectBrushDisabledIgnored
            << " subjectBrushDisabledStrokeCount=" << subjectBrushDisabledPayload.integratedToneLayerJson.value("autoSubjectSceneImportanceStrokeCount", -1)
            << " subjectBrushDisabledGuidanceStatus=" << subjectBrushDisabledPayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStatus", std::string())
            << " subjectBrushReduceGuidanceDiagnosticsWritten=" << subjectBrushReduceGuidanceDiagnosticsWritten
            << " subjectBrushReduceStatus=" << subjectBrushReducePayload.integratedToneLayerJson.value("autoSubjectSceneBrushStatus", std::string())
            << " subjectBrushReduceGuidanceStrength=" << subjectBrushReducePayload.integratedToneLayerJson.value("autoSubjectSceneUserGuidanceStrength", -1.0f)
            << " subjectBrushReduceIgnore=" << subjectBrushReducePayload.integratedToneLayerJson.value("autoSubjectSceneImportanceIgnore", -1.0f)
            << " subjectBrushReduceScoreComponentsWritten=" << subjectBrushReduceScoreComponentsWritten
            << " subjectReadableMidsGenerated=" << subjectReadableMidsGenerated
            << " subjectReadableMidsEligible=" << subjectReadableMidsEligible
            << " subjectReadableMidsHumanReadable=" << subjectReadableMidsHumanReadable
            << " subjectReadableMidsDiagnosticsWritten=" << subjectReadableMidsDiagnosticsWritten
            << " subjectReadableMidsRenderPayloadConstrained=" << subjectReadableMidsRenderPayloadConstrained
            << " subjectReadableMidsStageConstraint=" << subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " subjectReadableMidsSubjectProbe=" << subjectReadableMidsPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string())
            << " subjectReadableMidsMaxEvBias=" << subjectReadableMidsPayload.scenePrepSettings.maxEvBias
            << " sceneMoodPreservationGenerated=" << sceneMoodPreservationGenerated
            << " sceneMoodPreservationEligible=" << sceneMoodPreservationEligible
            << " sceneMoodPreservationHumanReadable=" << sceneMoodPreservationHumanReadable
            << " sceneMoodPreservationDiagnosticsWritten=" << sceneMoodPreservationDiagnosticsWritten
            << " sceneMoodPreservationRenderPayloadConstrained=" << sceneMoodPreservationRenderPayloadConstrained
            << " sceneMoodPreservationStageConstraint=" << sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " sceneMoodPreservationSubjectProbe=" << sceneMoodPreservationPayload.integratedToneLayerJson.value("autoCandidateSubjectIntentProbe", std::string())
            << " sceneMoodPreservationMaxEvBias=" << sceneMoodPreservationPayload.scenePrepSettings.maxEvBias
            << " haloSafeLocalRangeGenerated=" << haloSafeLocalRangeGenerated
            << " haloSafeLocalRangeEligible=" << haloSafeLocalRangeEligible
            << " haloSafeLocalRangeHumanReadable=" << haloSafeLocalRangeHumanReadable
            << " haloSafeLocalRangeDiagnosticsWritten=" << haloSafeLocalRangeDiagnosticsWritten
            << " haloSafeLocalRangeRenderPayloadConstrained=" << haloSafeLocalRangeRenderPayloadConstrained
            << " haloSafeLocalRangeNeed=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalHaloGuardNeed", -1.0f)
            << " haloSafeLocalRangeStageConstraint=" << haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " haloSafeLocalRangeScenePrepProbe=" << haloSafeLocalRangePayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " haloSafeLocalRangeMaxEvBias=" << haloSafeLocalRangePayload.scenePrepSettings.maxEvBias
            << " haloSafeLocalRangeHaloGuard=" << haloSafeLocalRangePayload.scenePrepSettings.haloGuard
            << " haloSafeLocalRangeSmoothGradient=" << haloSafeLocalRangePayload.scenePrepSettings.smoothGradientProtection
            << " haloSafeLocalRangeEdgeAwareness=" << haloSafeLocalRangePayload.scenePrepSettings.edgeAwareness
            << " localRangeGuardGenerated=" << localRangeGuardGenerated
            << " localRangeGuardEligible=" << localRangeGuardEligible
            << " localRangeGuardDiagnosticsWritten=" << localRangeGuardDiagnosticsWritten
            << " localRangeGuardRenderPayloadConstrained=" << localRangeGuardRenderPayloadConstrained
            << " localRangeGuardStageConstraint=" << localRangeGuardPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " localRangeGuardRegionSource=" << regionalEvidence.value("source", std::string())
            << " localRangeGuardLocalConflict=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalRangeConflict", -1.0f)
            << " localRangeGuardLocalEvConflict=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalEvConflict", -1.0f)
            << " localRangeGuardLocalEvSpreadStops=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeLocalEvSpreadStops", -1.0f)
            << " shadowReadabilityLiftGenerated=" << shadowReadabilityLiftGenerated
            << " shadowReadabilityLiftEligible=" << shadowReadabilityLiftEligible
            << " shadowReadabilityLiftHumanReadable=" << shadowReadabilityLiftHumanReadable
            << " shadowReadabilityLiftDiagnosticsWritten=" << shadowReadabilityLiftDiagnosticsWritten
            << " shadowReadabilityLiftRenderPayloadConstrained=" << shadowReadabilityLiftRenderPayloadConstrained
            << " shadowReadabilityLiftNeed=" << readableShadowPayload.integratedToneLayerJson.value("autoDynamicRangeShadowReadabilityLiftNeed", -1.0f)
            << " shadowReadabilityLiftStageConstraint=" << shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " shadowReadabilityLiftScenePrepProbe=" << shadowReadabilityLiftPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " shadowReadabilityLiftMaxEvBias=" << shadowReadabilityLiftPayload.scenePrepSettings.maxEvBias
            << " shadowReadabilityLiftNoiseBias=" << shadowReadabilityLiftPayload.scenePrepSettings.noiseProtectionBias
            << " shadowReadabilityLiftShadowLimitBias=" << shadowReadabilityLiftPayload.scenePrepSettings.shadowLiftLimitBias
            << " shadowNoiseFloorGenerated=" << shadowNoiseFloorGenerated
            << " shadowNoiseFloorEligible=" << shadowNoiseFloorEligible
            << " shadowNoiseFloorDiagnosticsWritten=" << shadowNoiseFloorDiagnosticsWritten
            << " shadowNoiseFloorRenderPayloadConstrained=" << shadowNoiseFloorRenderPayloadConstrained
            << " shadowNoiseFloorNeed=" << regionalEvidencePayload.integratedToneLayerJson.value("autoDynamicRangeShadowNoiseFloorNeed", -1.0f)
            << " shadowNoiseFloorStageConstraint=" << shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " shadowNoiseFloorScenePrepProbe=" << shadowNoiseFloorPayload.integratedToneLayerJson.value("autoCandidateScenePrepProbe", std::string())
            << " shadowNoiseFloorMaxEvBias=" << shadowNoiseFloorPayload.scenePrepSettings.maxEvBias
            << " shadowNoiseFloorNoiseBias=" << shadowNoiseFloorPayload.scenePrepSettings.noiseProtectionBias
            << " shadowNoiseFloorShadowLimitBias=" << shadowNoiseFloorPayload.scenePrepSettings.shadowLiftLimitBias
            << " whiteBalanceProbeGenerated=" << whiteBalanceProbeGenerated
            << " whiteBalanceProbeEligible=" << whiteBalanceProbeEligible
            << " whiteBalanceProbeHumanReadable=" << whiteBalanceProbeHumanReadable
            << " whiteBalanceProbeDiagnosticsWritten=" << whiteBalanceProbeDiagnosticsWritten
            << " whiteBalanceProbeRenderPayloadDiverges=" << whiteBalanceProbeRenderPayloadDiverges
            << " whiteBalanceProbeId=" << whiteBalanceProbeRenderId
            << " whiteBalanceProbeMode=" << whiteBalanceProbeMode
            << " whiteBalanceProbePayloadMode=" << Raw::WhiteBalanceModeName(whiteBalanceProbePayload.settings.whiteBalanceMode)
            << " whiteBalanceProbeStageConstraint=" << whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " whiteBalanceProbeDiagnosticId=" << whiteBalanceProbePayload.integratedToneLayerJson.value("autoCandidateWhiteBalanceProbe", std::string())
            << " scenePrepCandidateStageConstrained=" << scenePrepCandidateStageConstrained
            << " scenePrepStageConstraint=" << scenePrepStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " scenePrepStageExposure=" << scenePrepStagePayload.settings.exposureStops
            << " scenePrepStageBaseExposure=" << stageConstraintBasePayload.settings.exposureStops
            << " scenePrepStageBaseEvBias=" << scenePrepStagePayload.scenePrepSettings.baseEvBias
            << " scenePrepStageMaxEvBias=" << scenePrepStagePayload.scenePrepSettings.maxEvBias
            << " finishToneCandidateStageConstrained=" << finishToneCandidateStageConstrained
            << " finishToneStageConstraint=" << finishToneStagePayload.integratedToneLayerJson.value("autoCandidateStageConstraint", std::string())
            << " finishToneStageExposure=" << finishToneStagePayload.settings.exposureStops
            << " finishToneStageBaseEvBias=" << finishToneStagePayload.scenePrepSettings.baseEvBias
            << " finishToneStageMaxEvBias=" << finishToneStagePayload.scenePrepSettings.maxEvBias
            << " finishToneContrastBias=" << finishToneStagePayload.integratedToneLayerJson.value("autoContrastBias", -99.0f)
            << " renderedStageRelevanceWorks=" << renderedStageRelevanceWorks
            << " renderedRefineIntentRelevanceWorks=" << renderedRefineIntentRelevanceWorks
            << " renderedStageCacheValidationWorks=" << renderedStageCacheValidationWorks
            << " finishToneObservedDirtyBoundary=" << finishToneObservedDirtyBoundary
            << " finishToneStageCacheExpected=" << finishToneStageCacheExpected
            << " finishToneStageCacheStatus=" << finishToneStageCacheStatus
            << " finishToneStageCacheMet=" << finishToneStageCacheMet
            << " scenePrepObservedDirtyBoundary=" << scenePrepObservedDirtyBoundary
            << " scenePrepStageCacheExpected=" << scenePrepStageCacheExpected
            << " scenePrepStageCacheStatus=" << scenePrepStageCacheStatus
            << " scenePrepStageCacheMet=" << scenePrepStageCacheMet
            << " scenePrepMissObservedDirtyBoundary=" << scenePrepMissObservedDirtyBoundary
            << " scenePrepStageCacheMissStatus=" << scenePrepStageCacheMissStatus
            << " scenePrepStageCacheMissMet=" << scenePrepStageCacheMissMet
            << " rawStageObservedDirtyBoundary=" << rawStageObservedDirtyBoundary
            << " rawStageCacheExpected=" << rawStageCacheExpected
            << " rawStageCacheStatus=" << rawStageCacheStatus
            << " rawStageCacheMet=" << rawStageCacheMet
            << " renderedStageSchedulerClassificationWorks=" << renderedStageSchedulerClassificationWorks
            << " developCandidateRenderBudgetAllowsMultiNodeCoverage=" << developCandidateRenderBudgetAllowsMultiNodeCoverage
            << " developCandidateMetricReadbackBudgetWorks=" << developCandidateMetricReadbackBudgetWorks
            << " rawDevelopStageCacheMemoryPolicyWorks=" << rawDevelopStageCacheMemoryPolicyWorks
            << " developAdaptiveRenderBudgetWorks=" << developAdaptiveRenderBudgetWorks
            << " adaptiveContinueBudget=" << adaptiveContinueBudget
            << " adaptiveContinueReason=" << adaptiveContinueReason
            << " adaptiveContinueExpanded=" << adaptiveContinueExpanded
            << " adaptiveInitialBudget=" << adaptiveInitialBudget
            << " adaptiveInitialReason=" << adaptiveInitialReason
            << " adaptiveInitialExpanded=" << adaptiveInitialExpanded
            << " adaptiveFocusedBudget=" << adaptiveFocusedBudget
            << " adaptiveFocusedReason=" << adaptiveFocusedReason
            << " adaptiveFocusedExpanded=" << adaptiveFocusedExpanded
            << " adaptiveFocusedNarrowed=" << adaptiveFocusedNarrowed
            << " developCandidateFeedbackGateDropsStale=" << developCandidateFeedbackGateDropsStale
            << " developCandidateFeedbackGateDefersRecent=" << developCandidateFeedbackGateDefersRecent
            << " developCandidateFeedbackGateAppliesAfterQuiet=" << developCandidateFeedbackGateAppliesAfterQuiet
            << " developCandidateRenderAdmissionDefersRecent=" << developCandidateRenderAdmissionDefersRecent
            << " developCandidateFeedbackQuietRemainingWorks=" << developCandidateFeedbackQuietRemainingWorks
            << " candidateFeedbackRemainingMidEdit=" << candidateFeedbackRemainingMidEdit
            << " developStaleSnapshotAbortWorks=" << developStaleSnapshotAbortWorks
            << " developCandidateProgressLabelWorks=" << developCandidateProgressLabelWorks
            << " candidateProgressLabel=\"" << candidateProgressLabel << "\""
            << " candidateFeedbackQuietSeconds=" << candidateFeedbackQuietSeconds
            << " selectedScheduleRank=" << selectedScheduleRank
            << " finishToneScheduleRank=" << finishToneScheduleRank
            << " scenePrepScheduleRank=" << scenePrepScheduleRank
            << " rawGlobalScheduleRank=" << rawGlobalScheduleRank
            << " multiStageScheduleRank=" << multiStageScheduleRank
            << " finishToneScheduleBoundary=" << finishToneScheduleBoundary
            << " scenePrepScheduleBoundary=" << scenePrepScheduleBoundary
            << " rawGlobalScheduleBoundary=" << rawGlobalScheduleBoundary
            << " cleanProbeLumaDenoise=" << cleanProbePayload.settings.mosaicDenoise.lumaStrength
            << " textureProbeLumaDenoise=" << textureProbePayload.settings.mosaicDenoise.lumaStrength
            << " cleanProbeFalseColor=" << cleanProbePayload.settings.falseColorSuppression
            << " textureProbeFalseColor=" << textureProbePayload.settings.falseColorSuppression
            << " cleanProbeTextureSensitivity=" << cleanProbePayload.scenePrepSettings.textureSensitivity
            << " textureProbeTextureSensitivity=" << textureProbePayload.scenePrepSettings.textureSensitivity
            << " brightnessExposureTelemetryForwarded=" << brightnessExposureTelemetryForwarded
            << " exposureDiagnosticsForwarded=" << exposureDiagnosticsForwarded
            << " renderedVisualRiskMetricsPopulated=" << renderedVisualRiskMetricsPopulated
            << " visualRiskContrastSpan=" << visualRiskMetrics.contrastSpan
            << " visualRiskLowSaturation=" << visualRiskMetrics.lowSaturationFraction
            << " visualRiskEdgeContrast=" << visualRiskMetrics.edgeContrast
            << " visualRiskHalo=" << visualRiskMetrics.haloRiskFraction
            << " visualRiskShadowTexture=" << visualRiskMetrics.shadowTextureRisk
            << " renderedHighlightGrayMetricsPopulated=" << renderedHighlightGrayMetricsPopulated
            << " visualRiskHighlightBandFraction=" << visualRiskMetrics.highlightBandFraction
            << " visualRiskHighlightMeanLuma=" << visualRiskMetrics.highlightMeanLuma
            << " visualRiskHighlightLowSaturationFraction=" << visualRiskMetrics.highlightLowSaturationFraction
            << " visualRiskHighlightGrayRisk=" << visualRiskMetrics.highlightGrayRisk
            << " renderedMeaningfulHighlightMetricsPopulated=" << renderedMeaningfulHighlightMetricsPopulated
            << " visualRiskHighlightTileCoverage=" << visualRiskMetrics.highlightTileCoverage
            << " visualRiskHighlightStructureScore=" << visualRiskMetrics.highlightStructureScore
            << " visualRiskMeaningfulHighlightPressure=" << visualRiskMetrics.meaningfulHighlightPressure
            << " renderedLocalMetricsPopulated=" << renderedLocalMetricsPopulated
            << " renderedLocalLumaSpread=" << visualRiskMetrics.localLumaSpread
            << " renderedLocalEvSpreadStops=" << visualRiskMetrics.localEvSpreadStops
            << " renderedLocalEvConflict=" << visualRiskMetrics.localEvConflict
            << " renderedLocalContrastPeak=" << visualRiskMetrics.localContrastPeak
            << " renderedLocalShadowPressure=" << visualRiskMetrics.localShadowPressure
            << " renderedCenterMeanLuma=" << visualRiskMetrics.centerMeanLuma
            << " renderedSubjectMetricsPopulated=" << renderedSubjectMetricsPopulated
            << " subjectRiskCenterPrior=" << subjectRiskMetrics.subjectCenterPrior
            << " subjectRiskConfidence=" << subjectRiskMetrics.subjectImportanceConfidence
            << " subjectRiskReadability=" << subjectRiskMetrics.subjectReadabilityPressure
            << " subjectRiskProtection=" << subjectRiskMetrics.subjectProtectionPressure
            << " subjectRiskMood=" << subjectRiskMetrics.subjectMoodPreservationPressure
            << " renderedMarkedSubjectMetricsPopulated=" << renderedMarkedSubjectMetricsPopulated
            << " markedSubjectCoverage=" << markedSubjectMetrics.subjectMarkedCoverage
            << " markedSubjectPositiveCoverage=" << markedSubjectMetrics.subjectMarkedPositiveCoverage
            << " markedSubjectRevealCoverage=" << markedSubjectMetrics.subjectMarkedRevealCoverage
            << " markedSubjectMeanLuma=" << markedSubjectMetrics.subjectMarkedMeanLuma
            << " markedSubjectReadabilityScore=" << markedSubjectMetrics.subjectMarkedReadabilityScore
            << " markedSubjectProtectionRisk=" << markedSubjectMetrics.subjectMarkedProtectionRisk
            << " renderedMarkedLowPriorityMetricsPopulated=" << renderedMarkedLowPriorityMetricsPopulated
            << " markedLowPriorityCoverage=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityCoverage
            << " markedLowPriorityMeanLuma=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityMeanLuma
            << " markedLowPriorityBrightFraction=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityBrightFraction
            << " markedLowPriorityPressure=" << lowPrioritySubjectMetrics.subjectMarkedLowPriorityPressure
            << " renderedSpatialRiskMetricsPopulated=" << renderedSpatialRiskMetricsPopulated
            << " renderedLocalDamageRiskMean=" << spatialRiskMetrics.localDamageRiskMean
            << " renderedLocalDamageRiskPeak=" << spatialRiskMetrics.localDamageRiskPeak
            << " renderedLocalDamageRiskPeakTile=" << spatialRiskMetrics.localDamageRiskPeakTile
            << " renderedColorCastMetricsPopulated=" << renderedColorCastMetricsPopulated
            << " colorCastMeanRed=" << colorCastMetrics.meanRed
            << " colorCastMeanGreen=" << colorCastMetrics.meanGreen
            << " colorCastMeanBlue=" << colorCastMetrics.meanBlue
            << " colorCastWarmCoolBias=" << colorCastMetrics.warmCoolBias
            << " colorCastMagentaGreenBias=" << colorCastMetrics.magentaGreenBias
            << " colorCastChannelImbalance=" << colorCastMetrics.channelImbalance
            << " colorCastRisk=" << colorCastMetrics.colorCastRisk
            << " renderedDuplicateMetricDistanceWorks=" << renderedDuplicateMetricDistanceWorks
            << " renderedDuplicateMetricDistance=" << renderedDuplicateMetricDistance
            << " renderedDistinctMetricDistance=" << renderedDistinctMetricDistance
            << " renderedLocalMetricDistance=" << renderedLocalMetricDistance
            << " renderedColorMetricDistance=" << renderedColorMetricDistance
            << " renderedMarkedSubjectMetricDistance=" << renderedMarkedSubjectMetricDistance
            << " renderedStageBoundaryClassifierWorks=" << renderedStageBoundaryClassifierWorks
            << " finishOnlyStageBoundary=" << finishOnlyStageBoundary
            << " finishOnlyFinalMetricDistance=" << finishOnlyFinalMetricDistance
            << " finishOnlyPreFinishMetricDistance=" << finishOnlyPreFinishMetricDistance
            << " preFinishChangedStageBoundary=" << preFinishChangedStageBoundary
            << " preFinishChangedFinalMetricDistance=" << preFinishChangedFinalMetricDistance
            << " preFinishChangedPreFinishMetricDistance=" << preFinishChangedPreFinishMetricDistance
            << " renderedStageAwareDuplicateClusteringWorks=" << renderedStageAwareDuplicateClusteringWorks
            << " stageAwareDuplicateFinalDistance=" << stageAwareDuplicateFinalDistance
            << " stageAwareDuplicatePreFinishDistance=" << stageAwareDuplicatePreFinishDistance
            << " stageAwareDuplicatePreFinishDistinct=" << stageAwareDuplicatePreFinishDistinct
            << " stageAwareMaskedFinalDistance=" << stageAwareMaskedFinalDistance
            << " stageAwareMaskedPreFinishDistance=" << stageAwareMaskedPreFinishDistance
            << " stageAwareMaskedPreFinishDistinct=" << stageAwareMaskedPreFinishDistinct
            << " renderedDamageClassifierWorks=" << renderedDamageClassifierWorks
            << " damagedClipReason=" << damagedClipReason
            << " damagedHaloReason=" << damagedHaloReason
            << " damagedGrayReason=" << damagedGrayReason
            << " damagedShadowNoiseReason=" << damagedShadowNoiseReason
            << " damagedSpatialHotspotReason=" << damagedSpatialHotspotReason
            << " damagedColorCastReason=" << damagedColorCastReason
            << " safeRenderedDamageReason=" << safeRenderedDamageReason
            << " renderedRelativeComparisonWorks=" << renderedRelativeComparisonWorks
            << " relativeAdjustedRawScore=" << relativeAdjustedRawScore
            << " relativeAdjustedIntentScore=" << relativeAdjustedIntentScore
            << " relativeRawScoreStatus=" << relativeRawScoreStatus
            << " relativeIntentStatus=" << relativeIntentStatus
            << " relativeRawScoreRepairDelta=" << relativeRawScoreRepairDelta
            << " relativeIntentRepairDelta=" << relativeIntentRepairDelta
            << " relativeRawScoreRegressionPenalty=" << relativeRawScoreRegressionPenalty
            << " modeNeighborCandidateGenerated=" << modeNeighborCandidateGenerated
            << " modeNeighborCandidateEligible=" << modeNeighborCandidateEligible
            << " modeNeighborCandidateHumanReadable=" << modeNeighborCandidateHumanReadable
            << " modeNeighborCandidateMeaningfullyDifferent=" << modeNeighborCandidateMeaningfullyDifferent
            << " renderedLocalRefineIntentWorks=" << renderedLocalRefineIntentWorks
            << " localCenterShadowIntent=" << localCenterShadowIntent
            << " localHighlightIntent=" << localHighlightIntent
            << " structuredHighlightPressureIntent=" << structuredHighlightPressureIntent
            << " structuredHighlightPressureReason=" << structuredHighlightPressureReason
            << " localSpatialHighlightRiskIntent=" << localSpatialHighlightRiskIntent
            << " localSpatialHighlightRiskReason=" << localSpatialHighlightRiskReason
            << " localFlatIntent=" << localFlatIntent
            << " renderedCleanShadowIntent=" << renderedCleanShadowIntent
            << " localSpatialShadowRiskIntent=" << localSpatialShadowRiskIntent
            << " localSpatialShadowRiskReason=" << localSpatialShadowRiskReason
            << " localSpatialFlatRiskIntent=" << localSpatialFlatRiskIntent
            << " localSpatialFlatRiskReason=" << localSpatialFlatRiskReason
            << " renderedPreserveTextureIntent=" << renderedPreserveTextureIntent
            << " renderedFeedbackCandidateId=" << renderedFeedbackCandidateId
            << " renderedFeedbackAdoptedCandidate=" << renderedFeedbackAdoptedCandidate
            << " renderedFeedbackLoopActive=" << renderedFeedbackLoopActive
            << " renderedFeedbackLoopState=" << renderedFeedbackLoop.value("state", std::string())
            << " renderedFeedbackLoopAction=" << renderedFeedbackLoop.value("action", std::string())
            << " renderedFeedbackLoopNextStep=" << renderedFeedbackLoop.value("nextStep", std::string())
            << " renderedContinuationPolicyContinues=" << renderedContinuationPolicyContinues
            << " renderedFeedbackContinuationDecision=" << renderedFeedbackContinuation.value("decision", std::string())
            << " renderedFeedbackContinuationReason=" << renderedFeedbackContinuation.value("reason", std::string())
            << " renderedFeedbackContinuationNextStep=" << renderedFeedbackContinuation.value("nextStep", std::string())
            << " convergenceEvidenceContinuesAfterFeedback=" << convergenceEvidenceContinuesAfterFeedback
            << " renderedFeedbackConvergenceState=" << renderedFeedbackConvergenceEvidence.value("state", std::string())
            << " renderedFeedbackConvergenceDecision=" << renderedFeedbackConvergenceEvidence.value("decision", std::string())
            << " renderedFeedbackConvergenceReason=" << renderedFeedbackConvergenceEvidence.value("reason", std::string())
            << " convergenceEvidenceAdmissionTightensMarginalContinuation=" << convergenceEvidenceAdmissionTightensMarginalContinuation
            << " convergenceAdmissionStopReason=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " convergenceAdmissionTightened=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionTightened", false)
            << " convergenceAdmissionMinimum=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionMinimumImprovement", -1.0f)
            << " convergenceAdmissionBaseMinimum=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionBaseMinimumImprovement", -1.0f)
            << " convergenceAdmissionEvidenceState=" << convergenceAdmissionPayload.integratedToneLayerJson.value("autoCandidateConvergenceAdmissionEvidenceState", std::string())
            << " convergenceAdmissionConvergenceState=" << convergenceAdmissionEvidence.value("state", std::string())
            << " convergenceAdmissionChallengerId=" << convergenceAdmissionChallengerId
            << " continuationBiasDiagnosticsWritten=" << continuationBiasDiagnosticsWritten
            << " continuationBiasBoostsFinishToneCandidates=" << continuationBiasBoostsFinishToneCandidates
            << " continuationBiasActive=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasActive", false)
            << " continuationBiasReason=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasReason", std::string())
            << " continuationBiasStage=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasStage", std::string())
            << " continuationBiasAppliedCount=" << continuationBiasPayload.integratedToneLayerJson.value("autoCandidateContinuationBiasAppliedCount", -1)
            << " continuationExpansionDiagnosticsWritten=" << continuationExpansionDiagnosticsWritten
            << " continuationExpansionAddsFinishToneFamily=" << continuationExpansionAddsFinishToneFamily
            << " continuationExpansionEligible=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionEligible", false)
            << " continuationExpansionActive=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionActive", false)
            << " continuationExpansionStage=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionStage", std::string())
            << " continuationExpansionAddedCount=" << continuationExpansionPayload.integratedToneLayerJson.value("autoCandidateContinuationExpansionAddedCount", -1)
            << " renderedFeedbackSelectedId=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedFeedbackSelectionSource=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedFeedbackPass=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedFeedbackRevisionStageWritten=" << renderedFeedbackRevisionStageWritten
            << " renderedFeedbackRevisionStage=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedFeedbackFinishToneStageOverride=" << renderedFeedbackFinishToneStageOverride
            << " renderedFeedbackRevisionReason=" << renderedFeedbackPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionReason", std::string())
            << " renderedFeedbackMergedCandidate=" << renderedFeedbackMergedCandidate
            << " renderedMergeSelectedId=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedMergeSelectionSource=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedMergeFeedbackAction=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedMergePass=" << renderedMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedFeedbackPairMergedCandidate=" << renderedFeedbackPairMergedCandidate
            << " renderedFeedbackSecondCandidateId=" << renderedFeedbackSecondCandidateId
            << " renderedFeedbackEnsembleMergedCandidate=" << renderedFeedbackEnsembleMergedCandidate
            << " renderedFeedbackThirdCandidateId=" << renderedFeedbackThirdCandidateId
            << " renderedEnsembleMergeSelectedId=" << renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedEnsembleMergeSelectionSource=" << renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedEnsembleMergeThirdId=" << renderedEnsembleMergePayload.integratedToneLayerJson.value("autoCandidateMergeThirdId", std::string())
            << " renderedEnsembleMergeWeights="
            << renderedEnsembleFirstWeight << ","
            << renderedEnsembleSecondWeight << ","
            << renderedEnsembleThirdWeight
            << " renderedPairMergeSelectedId=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedPairMergeSelectionSource=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedPairMergeFeedbackAction=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedPairMergeFirstId=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeFirstId", std::string())
            << " renderedPairMergeSecondId=" << renderedPairMergePayload.integratedToneLayerJson.value("autoCandidateMergeSecondId", std::string())
            << " renderedFeedbackCandidateCarriedForward=" << renderedFeedbackCandidateCarriedForward
            << " renderedSurvivorCandidateCarriedForward=" << renderedSurvivorCandidateCarriedForward
            << " renderedSurvivorCarryForwardCountWritten=" << renderedSurvivorCarryForwardCountWritten
            << " renderedSurvivorCarryForwardCount=" << renderedSurvivorCarryPayload.integratedToneLayerJson.value("autoCandidateRenderedCarriedForwardCount", -1)
            << " renderedFeedbackRefinedCandidate=" << renderedFeedbackRefinedCandidate
            << " renderedRefineRevisionStageIsScenePrep=" << renderedRefineRevisionStageIsScenePrep
            << " renderedRefineStagePlanTargetsScenePrep=" << renderedRefineStagePlanTargetsScenePrep
            << " renderedRefineAutoStageEarliestDirtyStage=" << renderedRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string())
            << " renderedRefineAutoStageRevisionStage=" << renderedRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string())
            << " renderedRefineAutoStageResponsibleRevisionState=" << renderedRefinePayload.integratedToneLayerJson.value("autoStageResponsibleRevisionState", std::string())
            << " renderedRefineRevisionStage=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedLocalRefineCandidateGenerated=" << renderedLocalRefineCandidateGenerated
            << " renderedRefineSelectedId=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedRefineSelectionSource=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedRefineFeedbackAction=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedRefineIntent=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string())
            << " renderedRefinePass=" << renderedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedCleanupRefinedCandidate=" << renderedCleanupRefinedCandidate
            << " renderedCleanupRevisionStageIsRawCleanup=" << renderedCleanupRevisionStageIsRawCleanup
            << " renderedCleanupStagePlanTargetsRawBase=" << renderedCleanupStagePlanTargetsRawBase
            << " renderedCleanupAutoStageEarliestDirtyStage=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageEarliestDirtyStage", std::string())
            << " renderedCleanupAutoStageRevisionStage=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageRevisionStage", std::string())
            << " renderedCleanupAutoStageResponsibleRevisionState=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoStageResponsibleRevisionState", std::string())
            << " renderedCleanupRevisionStage=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedCleanupRefineCandidateGenerated=" << renderedCleanupRefineCandidateGenerated
            << " renderedCleanupRefineSelectedId=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedCleanupRefineSelectionSource=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedCleanupRefineFeedbackAction=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackAction", std::string())
            << " renderedCleanupRefineIntent=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string())
            << " renderedCleanupRefineStopReason=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedCleanupRefineRenderedFingerprint=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0))
            << " renderedCleanupRefineSolveFingerprint=" << renderedCleanupRefinePayload.integratedToneLayerJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0))
            << " renderedRepeatedRefineStops=" << renderedRepeatedRefineStops
            << " renderedRepeatedRefineSelectedId=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedRepeatedRefineSelectionSource=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateSelectionSource", std::string())
            << " renderedRepeatedRefineApplied=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true)
            << " renderedRepeatedRefinePass=" << renderedRepeatedRefinePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedMonotonicShadowRiskStops=" << renderedMonotonicShadowRiskStops
            << " renderedMonotonicShadowStopReason=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedMonotonicShadowStatus=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string())
            << " renderedMonotonicShadowMetric=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicMetric", std::string())
            << " renderedMonotonicShadowPrevious=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicPreviousValue", -1.0f)
            << " renderedMonotonicShadowCurrent=" << renderedMonotonicShadowPayload.integratedToneLayerJson.value("autoCandidateRenderedMonotonicCurrentValue", -1.0f)
            << " renderedMonotonicShadowLoopState=" << renderedMonotonicShadowLoop.value("state", std::string())
            << " renderedNoImprovementStops=" << renderedNoImprovementStops
            << " renderedNoImprovementSelectedId=" << renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " renderedNoImprovementApplied=" << renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackApplied", true)
            << " renderedNoImprovementPass=" << renderedNoImprovementPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackPass", -1)
            << " renderedStableMetricsConverge=" << renderedStableMetricsConverge
            << " renderedStableLoopConverged=" << renderedStableLoopConverged
            << " renderedStableLoopState=" << renderedStableLoop.value("state", std::string())
            << " renderedStableLoopStopReason=" << renderedStableLoop.value("stopReason", std::string())
            << " renderedStableStopReason=" << renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedContinuationPolicyStopsStable=" << renderedContinuationPolicyStopsStable
            << " renderedStableContinuationDecision=" << renderedStableContinuation.value("decision", std::string())
            << " renderedStableContinuationReason=" << renderedStableContinuation.value("reason", std::string())
            << " renderedStableDistance=" << renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityDistance", -1.0f)
            << " renderedStableStatus=" << renderedStablePayload.integratedToneLayerJson.value("autoCandidateRenderedStabilityStatus", std::string())
            << " convergenceEvidenceStopsStable=" << convergenceEvidenceStopsStable
            << " renderedStableConvergenceState=" << renderedStableConvergenceEvidence.value("state", std::string())
            << " renderedStableConvergenceReason=" << renderedStableConvergenceEvidence.value("reason", std::string())
            << " renderedTrendConverges=" << renderedTrendConverges
            << " renderedTrendLoopConverged=" << renderedTrendLoopConverged
            << " renderedTrendLoopState=" << renderedTrendLoop.value("state", std::string())
            << " renderedTrendLoopStopReason=" << renderedTrendLoop.value("stopReason", std::string())
            << " renderedTrendStopReason=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedContinuationPolicyStopsTrend=" << renderedContinuationPolicyStopsTrend
            << " renderedTrendContinuationDecision=" << renderedTrendContinuation.value("decision", std::string())
            << " renderedTrendContinuationReason=" << renderedTrendContinuation.value("reason", std::string())
            << " renderedTrendStatus=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendStatus", std::string())
            << " renderedTrendHistoryCount=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendHistoryCount", -1)
            << " renderedTrendSameBestCount=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendSameBestCount", -1)
            << " renderedTrendScoreSpread=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendScoreSpread", -1.0f)
            << " renderedTrendNearestDistance=" << renderedTrendPayload.integratedToneLayerJson.value("autoCandidateRenderedTrendNearestDistance", -1.0f)
            << " convergenceEvidenceStopsTrend=" << convergenceEvidenceStopsTrend
            << " renderedTrendConvergenceState=" << renderedTrendConvergenceEvidence.value("state", std::string())
            << " renderedTrendConvergenceReason=" << renderedTrendConvergenceEvidence.value("reason", std::string())
            << " renderedFeedbackStopConvergenceClassifierWorks=" << renderedFeedbackStopConvergenceClassifierWorks
            << " renderedContinuationPolicyStopsAtPassLimit=" << renderedContinuationPolicyStopsAtPassLimit
            << " renderedPassLimitLoopState=" << renderedPassLimitLoop.value("state", std::string())
            << " renderedPassLimitStopReason=" << renderedPassLimitLoop.value("stopReason", std::string())
            << " renderedPassLimitContinuationDecision=" << renderedPassLimitContinuation.value("decision", std::string())
            << " renderedPassLimitContinuationReason=" << renderedPassLimitContinuation.value("reason", std::string())
            << " renderedPassLimitContinuationPass=" << renderedPassLimitContinuation.value("pass", -1)
            << " convergenceEvidenceStopsAtPassLimit=" << convergenceEvidenceStopsAtPassLimit
            << " renderedPassLimitConvergenceState=" << renderedPassLimitConvergenceEvidence.value("state", std::string())
            << " renderedPassLimitConvergenceReason=" << renderedPassLimitConvergenceEvidence.value("reason", std::string())
            << " renderedNoBestStopsWithoutConverging=" << renderedNoBestStopsWithoutConverging
            << " renderedNoBestStopReason=" << renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedFeedbackStopReason", std::string())
            << " renderedNoBestConverged=" << renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedConverged", true)
            << " renderedNoBestRevisionStage=" << renderedNoBestPayload.integratedToneLayerJson.value("autoCandidateRenderedRevisionStage", std::string())
            << " renderedNoBestLoopState=" << renderedNoBestLoop.value("state", std::string())
            << " renderedNoBestLoopStopReason=" << renderedNoBestLoop.value("stopReason", std::string())
            << " convergenceEvidenceStopsNoBest=" << convergenceEvidenceStopsNoBest
            << " renderedNoBestConvergenceState=" << renderedNoBestConvergenceEvidence.value("state", std::string())
            << " renderedNoBestConvergenceReason=" << renderedNoBestConvergenceEvidence.value("reason", std::string())
            << " partialSolvePreservedRawAuthorship=" << partialSolvePreservedRawAuthorship
            << " partialSolveUpdatedPrepAndFinish=" << partialSolveUpdatedPrepAndFinish
            << " repeatedFullSolveStable=" << repeatedFullSolveStable
            << " positiveExposureBiasPreserved=" << positiveExposureBiasPreserved
            << " highlightProfilePrefersHeadroom=" << highlightProfilePrefersHeadroom
            << " tinySpecularsDoNotDragExposure=" << tinySpecularsDoNotDragExposure
            << " broadHighlightsProtectedMoreThanSpeculars=" << broadHighlightsProtectedMoreThanSpeculars
            << " underBrightBroadHighlightsLifted=" << underBrightBroadHighlightsLifted
            << " darkSceneLifted=" << darkSceneLifted
            << " rawBaselineCarriesDarkLift=" << rawBaselineCarriesDarkLift
            << " rawBaselineDominatesDarkLift=" << rawBaselineDominatesDarkLift
            << " noisyProfilePrefersProtection=" << noisyProfilePrefersProtection
            << " modeIntentAffectsSolve=" << modeIntentAffectsSolve
            << " modeIntentForwarded=" << modeIntentForwarded
            << " exposureBefore=" << settingsBefore.exposureStops
            << " exposureAfter=" << payload.settings.exposureStops
            << " highlightModeAfter=" << static_cast<int>(payload.settings.highlightMode)
            << " highlightStrengthBefore=" << settingsBefore.highlightStrength
            << " highlightStrengthAfter=" << payload.settings.highlightStrength
            << " highlightThresholdBefore=" << settingsBefore.highlightThreshold
            << " highlightThresholdAfter=" << payload.settings.highlightThreshold
            << " lumaDenoiseBefore=" << settingsBefore.mosaicDenoise.lumaStrength
            << " lumaDenoiseAfter=" << payload.settings.mosaicDenoise.lumaStrength
            << " falseColorBefore=" << settingsBefore.falseColorSuppression
            << " falseColorAfter=" << payload.settings.falseColorSuppression
            << " autoBrightnessIntent=" << payload.integratedToneLayerJson.value("autoBrightnessIntent", -99.0f)
            << " autoRawExposurePreferenceEv=" << payload.integratedToneLayerJson.value("autoRawExposurePreferenceEv", -99.0f)
            << " autoAuthoredRawExposureEv=" << payload.integratedToneLayerJson.value("autoAuthoredRawExposureEv", -99.0f)
            << " autoAuthoredRawExposureScale=" << payload.integratedToneLayerJson.value("autoAuthoredRawExposureScale", -99.0f)
            << " autoAuthoredLocalMinEvBias=" << payload.integratedToneLayerJson.value("autoAuthoredLocalMinEvBias", -99.0f)
            << " autoAuthoredLocalMaxEvBias=" << payload.integratedToneLayerJson.value("autoAuthoredLocalMaxEvBias", -99.0f)
            << " autoExposureDiagnosticHighlightPressure=" << payload.integratedToneLayerJson.value("autoExposureDiagnosticHighlightPressure", -99.0f)
            << " autoExposureDiagnosticNoiseRisk=" << payload.integratedToneLayerJson.value("autoExposureDiagnosticNoiseRisk", -99.0f)
            << " autoExposureDiagnosticHdrSpreadEv=" << payload.integratedToneLayerJson.value("autoExposureDiagnosticHdrSpreadEv", -99.0f)
            << " autoCandidateSelectedId=" << payload.integratedToneLayerJson.value("autoCandidateSelectedId", std::string())
            << " autoCandidateSelectedLabel=" << payload.integratedToneLayerJson.value("autoCandidateSelectedLabel", std::string())
            << " autoCandidateSurvivorCount=" << payload.integratedToneLayerJson.value("autoCandidateSurvivorCount", -1)
            << " autoCandidateRejectedCount=" << payload.integratedToneLayerJson.value("autoCandidateRejectedCount", -1)
            << " autoCandidateConvergencePass=" << payload.integratedToneLayerJson.value("autoCandidateConvergencePass", -1)
            << " prepStrengthBefore=" << scenePrepBefore.strength
            << " prepStrengthAfter=" << payload.scenePrepSettings.strength
            << " prepMaxBiasBefore=" << scenePrepBefore.maxEvBias
            << " prepMaxBiasAfter=" << payload.scenePrepSettings.maxEvBias
            << " prepHighlightBiasBefore=" << scenePrepBefore.highlightProtectionBias
            << " prepHighlightBiasAfter=" << payload.scenePrepSettings.highlightProtectionBias
            << " balancedExposure=" << balancedPayload.settings.exposureStops
            << " neutralExposure=" << neutralExposurePayload.settings.exposureStops
            << " biasedExposure=" << biasedExposurePayload.settings.exposureStops
            << " neutralBaseEvBias=" << neutralExposurePayload.scenePrepSettings.baseEvBias
            << " biasedBaseEvBias=" << biasedExposurePayload.scenePrepSettings.baseEvBias
            << " highlightHeavyExposure=" << highlightHeavyPayload.settings.exposureStops
            << " specularExposure=" << specularPayload.settings.exposureStops
            << " underBrightHighlightExposure=" << underBrightHighlightPayload.settings.exposureStops
            << " darkSceneExposure=" << darkScenePayload.settings.exposureStops
            << " darkRawExposureLift=" << darkRawExposureLift
            << " darkScenePrepAmountLift=" << darkScenePrepAmountLift
            << " balancedHighlightThreshold=" << balancedPayload.settings.highlightThreshold
            << " highlightHeavyHighlightThreshold=" << highlightHeavyPayload.settings.highlightThreshold
            << " balancedHighlightBias=" << balancedPayload.scenePrepSettings.highlightProtectionBias
            << " highlightHeavyHighlightBias=" << highlightHeavyPayload.scenePrepSettings.highlightProtectionBias
            << " specularHighlightBias=" << specularPayload.scenePrepSettings.highlightProtectionBias
            << " underBrightHighlightBias=" << underBrightHighlightPayload.scenePrepSettings.highlightProtectionBias
            << " balancedMinEvBias=" << balancedPayload.scenePrepSettings.minEvBias
            << " highlightHeavyMinEvBias=" << highlightHeavyPayload.scenePrepSettings.minEvBias
            << " specularMinEvBias=" << specularPayload.scenePrepSettings.minEvBias
            << " balancedHighlightProtection=" << balancedPayload.scenePrepSettings.highlightProtection
            << " highlightHeavyHighlightProtection=" << highlightHeavyPayload.scenePrepSettings.highlightProtection
            << " darkSceneMaxEvBias=" << darkScenePayload.scenePrepSettings.maxEvBias
            << " darkSceneStrength=" << darkScenePayload.scenePrepSettings.strength
            << " darkSceneShadowLiftLimitBias=" << darkScenePayload.scenePrepSettings.shadowLiftLimitBias
            << " balancedNoiseBias=" << balancedPayload.scenePrepSettings.noiseProtectionBias
            << " noisyNoiseBias=" << noisyLowLightPayload.scenePrepSettings.noiseProtectionBias
            << " balancedMaxEvBias=" << balancedPayload.scenePrepSettings.maxEvBias
            << " flatMaxEvBias=" << flatIntentPayload.scenePrepSettings.maxEvBias
            << " balancedToneContrast=" << balancedPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f)
            << " flatToneContrast=" << flatIntentPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f)
            << " punchyToneContrast=" << punchyIntentPayload.integratedToneLayerJson.value("autoContrastBias", 0.0f)
            << " requestIdBefore=" << requestIdBefore
            << " requestIdAfter=" << payload.integratedToneLayerJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0))
            << "\n";
    }

    return success;
}

bool ValidateToneCurveAutoIntegration() {
    constexpr int kWidth = 320;
    constexpr int kHeight = 192;

    if (!ValidateDevelopAutoSolveBehavior()) {
        return false;
    }

    if (!glfwInit()) {
        std::cerr << "Tone Curve validation failed: glfwInit() failed.\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Tone Curve Validation", nullptr, nullptr);
    if (!window) {
        std::cerr << "Tone Curve validation failed: unable to create hidden OpenGL window.\n";
        glfwTerminate();
        return false;
    }

    bool success = false;
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Tone Curve validation failed: unable to load OpenGL functions.\n";
    } else {
        RenderPipeline pipeline;
        pipeline.Initialize();

        auto runGraph = [&](const std::vector<unsigned char>& pixels,
                            std::uint64_t requestRevision,
                            const nlohmann::json& layerJson,
                            std::vector<unsigned char>& outPixels,
                            int& outW,
                            int& outH,
                            float& outMaxRgb,
                            std::vector<ToneCurveAutoRewriteFeedback>& outFeedbacks) {
            pipeline.LoadSourceFromPixels(pixels.data(), kWidth, kHeight, 4);

            RenderGraphSnapshot graph;
            graph.outputNodeId = 3;

            RenderGraphNode imageNode;
            imageNode.nodeId = 1;
            imageNode.kind = RenderGraphNodeKind::Image;
            imageNode.requestRevision = requestRevision;
            imageNode.image.pixels = pixels;
            imageNode.image.width = kWidth;
            imageNode.image.height = kHeight;
            imageNode.image.channels = 4;
            graph.nodes.push_back(std::move(imageNode));

            RenderGraphNode toneNode;
            toneNode.nodeId = 2;
            toneNode.kind = RenderGraphNodeKind::Layer;
            toneNode.requestRevision = requestRevision;
            toneNode.layerJson = layerJson;
            graph.nodes.push_back(std::move(toneNode));

            RenderGraphNode outputNode;
            outputNode.nodeId = 3;
            outputNode.kind = RenderGraphNodeKind::Output;
            outputNode.requestRevision = requestRevision;
            graph.nodes.push_back(std::move(outputNode));

            graph.links.push_back(RenderGraphLink{ 1, "imageOut", 2, "imageIn" });
            graph.links.push_back(RenderGraphLink{ 2, "imageOut", 3, "imageIn" });

            pipeline.ExecuteGraph(graph);
            outMaxRgb = ReadTextureMaxRgb(pipeline.GetOutputTexture(), kWidth, kHeight);
            outPixels = pipeline.GetOutputPixels(outW, outH);
            outFeedbacks = pipeline.GetToneCurveAutoRewriteFeedback();
            if (outPixels.empty() || outW <= 0 || outH <= 0) {
                std::cerr << "Tone Curve validation failed: render produced invalid pixels.\n";
                return false;
            }
            return true;
        };

        ToneCurveLayer seedLayer;
        nlohmann::json seedJson = seedLayer.Serialize();
        seedJson["autoSceneAssistStrength"] = 1.0f;

        const std::size_t seedHash = HashJsonValue(seedJson);
        const std::vector<unsigned char> sceneA = BuildToneCurveValidationImage(kWidth, kHeight, false);
        const std::vector<unsigned char> sceneB = BuildToneCurveValidationImage(kWidth, kHeight, true);
        const std::vector<unsigned char> sceneBalanced = BuildToneCurveBalancedValidationImage(kWidth, kHeight);

        std::vector<unsigned char> outputManual;
        std::vector<unsigned char> outputA1;
        std::vector<unsigned char> outputA2;
        std::vector<unsigned char> outputB;
        std::vector<unsigned char> outputPreserved;
        std::vector<unsigned char> outputBalancedLow;
        std::vector<unsigned char> outputBalancedHigh;
        std::vector<unsigned char> outputHighlightLow;
        std::vector<unsigned char> outputHighlightHigh;
        int outW = 0;
        int outH = 0;
        float maxRgbManual = 0.0f;
        float maxRgbA1 = 0.0f;
        float maxRgbA2 = 0.0f;
        float maxRgbB = 0.0f;
        float maxRgbPreserved = 0.0f;
        float maxRgbBalancedLow = 0.0f;
        float maxRgbBalancedHigh = 0.0f;
        float maxRgbHighlightLow = 0.0f;
        float maxRgbHighlightHigh = 0.0f;
        std::vector<ToneCurveAutoRewriteFeedback> manualFeedbacks;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksA1;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksA2;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksB;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksPreserved;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksBalancedLow;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksBalancedHigh;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksHighlightLow;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksHighlightHigh;

        const bool manualPassOk = runGraph(sceneA, 1, seedJson, outputManual, outW, outH, maxRgbManual, manualFeedbacks);
        const bool manualPassHasNoRewrite = manualFeedbacks.empty();

        nlohmann::json calibrateJson = seedJson;
        calibrateJson["autoCalibratePending"] = true;
        calibrateJson["autoCalibrateRequestId"] = 1;
        calibrateJson["autoCalibrateVariant"] = 0;

        const bool firstPassOk = runGraph(sceneA, 2, calibrateJson, outputA1, outW, outH, maxRgbA1, feedbacksA1);
        const bool firstPassHasRewrite = feedbacksA1.size() == 1 && feedbacksA1.front().valid;
        const ToneCurveAutoRewriteFeedback feedbackA1 = firstPassHasRewrite ? feedbacksA1.front() : ToneCurveAutoRewriteFeedback{};
        const bool authoredChangedFromSeed = feedbackA1.authoredStateHash != 0 && feedbackA1.authoredStateHash != seedHash;
        const bool authoredClearedPending = !feedbackA1.authoredLayerJson.value("autoCalibratePending", true);

        const bool secondPassOk = runGraph(sceneA, 3, feedbackA1.authoredLayerJson, outputA2, outW, outH, maxRgbA2, feedbacksA2);
        const bool secondPassHasNoRewrite = feedbacksA2.empty();
        const bool convergedPixels = HashPixels(outputA1) == HashPixels(outputA2);

        nlohmann::json recalibrateJson = feedbackA1.authoredLayerJson;
        recalibrateJson["autoCalibratePending"] = true;
        recalibrateJson["autoCalibrateRequestId"] = 2;
        recalibrateJson["autoCalibrateVariant"] = 0;
        const bool thirdPassOk = runGraph(sceneB, 4, recalibrateJson, outputB, outW, outH, maxRgbB, feedbacksB);
        const bool thirdPassHasRewrite = feedbacksB.size() == 1 && feedbacksB.front().valid;
        const ToneCurveAutoRewriteFeedback feedbackB = thirdPassHasRewrite ? feedbacksB.front() : ToneCurveAutoRewriteFeedback{};
        const bool reactsToChangedInput =
            feedbackB.authoredStateHash != feedbackA1.authoredStateHash ||
            std::abs(feedbackB.shadowPercentile - feedbackA1.shadowPercentile) > 0.01f ||
            std::abs(feedbackB.highlightPercentile - feedbackA1.highlightPercentile) > 0.01f ||
            HashPixels(outputB) != HashPixels(outputA1);
        const float authoredEdgeProtectionB = feedbackB.authoredLayerJson.value("localEdgeProtection", 0.0f);
        const float authoredRangeProtectionB = feedbackB.authoredLayerJson.value("localRangeProtection", 0.0f);
        const bool darkHighlightProtectionElevated =
            authoredEdgeProtectionB >= 0.66f &&
            authoredRangeProtectionB >= 0.52f;

        nlohmann::json preservedJson = feedbackA1.authoredLayerJson;
        const float preservedLocalBaselineStrength = std::clamp(
            preservedJson.value("localBaselineStrength", 0.0f) + 0.18f,
            0.0f,
            1.6f);
        const float preservedFoundationShadows = std::clamp(
            preservedJson.value("foundationShadows", 0.0f) + 0.24f,
            -5.0f,
            5.0f);
        preservedJson["localBaselineStrength"] = preservedLocalBaselineStrength;
        preservedJson["foundationShadows"] = preservedFoundationShadows;
        preservedJson["autoCalibratePending"] = true;
        preservedJson["autoCalibrateRequestId"] = 5;
        preservedJson["autoCalibrateVariant"] = 0;
        const bool preservedPassOk = runGraph(sceneA, 5, preservedJson, outputPreserved, outW, outH, maxRgbPreserved, feedbacksPreserved);
        const bool preservedPassHasRewrite = feedbacksPreserved.size() == 1 && feedbacksPreserved.front().valid;
        const nlohmann::json preservedAuthored = preservedPassHasRewrite ? feedbacksPreserved.front().authoredLayerJson : nlohmann::json::object();
        const bool advancedControlsPreserved =
            std::abs(preservedAuthored.value("localBaselineStrength", -99.0f) - preservedLocalBaselineStrength) < 0.0015f &&
            std::abs(preservedAuthored.value("foundationShadows", -99.0f) - preservedFoundationShadows) < 0.0015f;

        nlohmann::json finishEditedJson = feedbackA1.authoredLayerJson;
        finishEditedJson["points"] = BuildCustomToneCurvePoints({
            { 0.0f, 0.0f },
            { 0.24f, 0.18f },
            { 0.52f, 0.58f },
            { 0.80f, 0.90f },
            { 1.0f, 1.0f }
        });
        finishEditedJson["autoCalibratePending"] = true;
        finishEditedJson["autoCalibrateRequestId"] = 10;
        finishEditedJson["autoCalibrateVariant"] = 0;
        std::vector<unsigned char> outputFinishPreserved;
        float maxRgbFinishPreserved = 0.0f;
        std::vector<ToneCurveAutoRewriteFeedback> feedbacksFinishPreserved;
        const bool finishPreservedOk = runGraph(sceneB, 10, finishEditedJson, outputFinishPreserved, outW, outH, maxRgbFinishPreserved, feedbacksFinishPreserved);
        const bool finishPreservedHasRewrite = feedbacksFinishPreserved.size() == 1 && feedbacksFinishPreserved.front().valid;
        const nlohmann::json finishPreservedAuthored = finishPreservedHasRewrite ? feedbacksFinishPreserved.front().authoredLayerJson : nlohmann::json::object();
        const bool finishGraphPreservedThroughRewrite =
            finishPreservedAuthored.value("points", nlohmann::json::array()) == finishEditedJson.value("points", nlohmann::json::array());

        nlohmann::json balancedLowJson = seedJson;
        balancedLowJson["autoSceneAssistStrength"] = 0.55f;
        balancedLowJson["autoCalibratePending"] = true;
        balancedLowJson["autoCalibrateRequestId"] = 6;
        balancedLowJson["autoCalibrateVariant"] = 0;
        nlohmann::json balancedHighJson = seedJson;
        balancedHighJson["autoSceneAssistStrength"] = 1.80f;
        balancedHighJson["autoCalibratePending"] = true;
        balancedHighJson["autoCalibrateRequestId"] = 7;
        balancedHighJson["autoCalibrateVariant"] = 0;
        const bool balancedLowOk = runGraph(sceneBalanced, 6, balancedLowJson, outputBalancedLow, outW, outH, maxRgbBalancedLow, feedbacksBalancedLow);
        const bool balancedHighOk = runGraph(sceneBalanced, 7, balancedHighJson, outputBalancedHigh, outW, outH, maxRgbBalancedHigh, feedbacksBalancedHigh);
        const bool balancedPassesHaveRewrite =
            feedbacksBalancedLow.size() == 1 && feedbacksBalancedLow.front().valid &&
            feedbacksBalancedHigh.size() == 1 && feedbacksBalancedHigh.front().valid;
        const float balancedLowMean = ComputeAverageNormalizedLuma(outputBalancedLow);
        const float balancedHighMean = ComputeAverageNormalizedLuma(outputBalancedHigh);
        const float balancedLowMiddleGrey = balancedPassesHaveRewrite ? feedbacksBalancedLow.front().authoredLayerJson.value("middleGrey", -1.0f) : -1.0f;
        const float balancedHighMiddleGrey = balancedPassesHaveRewrite ? feedbacksBalancedHigh.front().authoredLayerJson.value("middleGrey", -1.0f) : -1.0f;
        const bool balancedStrengthStaysNeutral =
            balancedHighMean >= balancedLowMean * 0.86f &&
            maxRgbBalancedHigh >= maxRgbBalancedLow * 0.78f &&
            balancedHighMiddleGrey + 0.020f >= balancedLowMiddleGrey;

        nlohmann::json highlightLowJson = seedJson;
        highlightLowJson["autoSceneAssistStrength"] = 1.20f;
        highlightLowJson["autoHighlightCharacter"] = -0.85f;
        highlightLowJson["autoCalibratePending"] = true;
        highlightLowJson["autoCalibrateRequestId"] = 8;
        highlightLowJson["autoCalibrateVariant"] = 0;
        nlohmann::json highlightHighJson = seedJson;
        highlightHighJson["autoSceneAssistStrength"] = 1.20f;
        highlightHighJson["autoHighlightCharacter"] = 0.85f;
        highlightHighJson["autoCalibratePending"] = true;
        highlightHighJson["autoCalibrateRequestId"] = 9;
        highlightHighJson["autoCalibrateVariant"] = 0;
        const bool highlightLowOk = runGraph(sceneB, 8, highlightLowJson, outputHighlightLow, outW, outH, maxRgbHighlightLow, feedbacksHighlightLow);
        const bool highlightHighOk = runGraph(sceneB, 9, highlightHighJson, outputHighlightHigh, outW, outH, maxRgbHighlightHigh, feedbacksHighlightHigh);
        const bool highlightPassesHaveRewrite =
            feedbacksHighlightLow.size() == 1 && feedbacksHighlightLow.front().valid &&
            feedbacksHighlightHigh.size() == 1 && feedbacksHighlightHigh.front().valid;
        const float highlightLowProtection = highlightPassesHaveRewrite ? feedbacksHighlightLow.front().authoredLayerJson.value("targetHighlightProtection", 99.0f) : 99.0f;
        const float highlightHighProtection = highlightPassesHaveRewrite ? feedbacksHighlightHigh.front().authoredLayerJson.value("targetHighlightProtection", -99.0f) : -99.0f;
        const float highlightLowFoundation = highlightPassesHaveRewrite ? feedbacksHighlightLow.front().authoredLayerJson.value("foundationHighlights", 99.0f) : 99.0f;
        const float highlightHighFoundation = highlightPassesHaveRewrite ? feedbacksHighlightHigh.front().authoredLayerJson.value("foundationHighlights", -99.0f) : -99.0f;
        const bool highlightCharacterResponds =
            highlightHighProtection + 0.03f < highlightLowProtection &&
            highlightHighFoundation > highlightLowFoundation + 0.03f;

        ToneCurveLayer autoRefreshLayer;
        autoRefreshLayer.Deserialize(finishEditedJson);
        const nlohmann::json autoRefreshBefore = autoRefreshLayer.Serialize();
        const std::uint64_t autoRefreshRequestIdBefore = autoRefreshBefore.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
        autoRefreshLayer.NotifyUpstreamDevelopChanged();
        const nlohmann::json autoRefreshAfter = autoRefreshLayer.Serialize();
        const std::uint64_t autoRefreshRequestIdAfter = autoRefreshAfter.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
        const bool developFollowRefreshQueued =
            autoRefreshAfter.value("autoCalibratePending", false) &&
            autoRefreshRequestIdAfter > autoRefreshRequestIdBefore;
        const bool finishGraphPreservedThroughDevelopRefresh =
            autoRefreshAfter.value("points", nlohmann::json::array()) == finishEditedJson.value("points", nlohmann::json::array());

        success =
            manualPassOk &&
            manualPassHasNoRewrite &&
            firstPassOk &&
            firstPassHasRewrite &&
            authoredChangedFromSeed &&
            authoredClearedPending &&
            secondPassOk &&
            secondPassHasNoRewrite &&
            convergedPixels &&
            thirdPassOk &&
            thirdPassHasRewrite &&
            reactsToChangedInput &&
            darkHighlightProtectionElevated &&
            preservedPassOk &&
            preservedPassHasRewrite &&
            advancedControlsPreserved &&
            finishPreservedOk &&
            finishPreservedHasRewrite &&
            finishGraphPreservedThroughRewrite &&
            balancedLowOk &&
            balancedHighOk &&
            balancedPassesHaveRewrite &&
            balancedStrengthStaysNeutral &&
            highlightLowOk &&
            highlightHighOk &&
            highlightPassesHaveRewrite &&
            highlightCharacterResponds &&
            developFollowRefreshQueued &&
            finishGraphPreservedThroughDevelopRefresh;

        if (!success) {
            const std::size_t hashManual = HashPixels(outputManual);
            const std::size_t hashA1 = HashPixels(outputA1);
            const std::size_t hashA2 = HashPixels(outputA2);
            const std::size_t hashB = HashPixels(outputB);
            std::cerr
                << "Tone Curve validation failed:"
                << " manualPassOk=" << manualPassOk
                << " manualPassHasNoRewrite=" << manualPassHasNoRewrite
                << " firstPassOk=" << firstPassOk
                << " firstPassHasRewrite=" << firstPassHasRewrite
                << " authoredChangedFromSeed=" << authoredChangedFromSeed
                << " authoredClearedPending=" << authoredClearedPending
                << " secondPassOk=" << secondPassOk
                << " secondPassHasNoRewrite=" << secondPassHasNoRewrite
                << " convergedPixels=" << convergedPixels
                << " thirdPassOk=" << thirdPassOk
                << " thirdPassHasRewrite=" << thirdPassHasRewrite
                << " reactsToChangedInput=" << reactsToChangedInput
                << " darkHighlightProtectionElevated=" << darkHighlightProtectionElevated
                << " preservedPassOk=" << preservedPassOk
                << " preservedPassHasRewrite=" << preservedPassHasRewrite
                << " advancedControlsPreserved=" << advancedControlsPreserved
                << " finishPreservedOk=" << finishPreservedOk
                << " finishPreservedHasRewrite=" << finishPreservedHasRewrite
                << " finishGraphPreservedThroughRewrite=" << finishGraphPreservedThroughRewrite
                << " balancedLowOk=" << balancedLowOk
                << " balancedHighOk=" << balancedHighOk
                << " balancedPassesHaveRewrite=" << balancedPassesHaveRewrite
                << " balancedStrengthStaysNeutral=" << balancedStrengthStaysNeutral
                << " highlightLowOk=" << highlightLowOk
                << " highlightHighOk=" << highlightHighOk
                << " highlightPassesHaveRewrite=" << highlightPassesHaveRewrite
                << " highlightCharacterResponds=" << highlightCharacterResponds
                << " developFollowRefreshQueued=" << developFollowRefreshQueued
                << " finishGraphPreservedThroughDevelopRefresh=" << finishGraphPreservedThroughDevelopRefresh
                << " hashManual=" << hashManual
                << " hashA1=" << hashA1
                << " hashA2=" << hashA2
                << " hashB=" << hashB
                << " maxRgbPreserved=" << maxRgbPreserved
                << " maxRgbBalancedLow=" << maxRgbBalancedLow
                << " maxRgbBalancedHigh=" << maxRgbBalancedHigh
                << " maxRgbHighlightLow=" << maxRgbHighlightLow
                << " maxRgbHighlightHigh=" << maxRgbHighlightHigh
                << " balancedLowMean=" << balancedLowMean
                << " balancedHighMean=" << balancedHighMean
                << " balancedLowMiddleGrey=" << balancedLowMiddleGrey
                << " balancedHighMiddleGrey=" << balancedHighMiddleGrey
                << " preservedLocalBaselineStrength=" << preservedLocalBaselineStrength
                << " preservedAuthoredLocalBaselineStrength=" << preservedAuthored.value("localBaselineStrength", -99.0f)
                << " preservedFoundationShadows=" << preservedFoundationShadows
                << " preservedAuthoredFoundationShadows=" << preservedAuthored.value("foundationShadows", -99.0f)
                << " highlightLowProtection=" << highlightLowProtection
                << " highlightHighProtection=" << highlightHighProtection
                << " highlightLowFoundation=" << highlightLowFoundation
                << " highlightHighFoundation=" << highlightHighFoundation
                << " autoRefreshRequestIdBefore=" << autoRefreshRequestIdBefore
                << " autoRefreshRequestIdAfter=" << autoRefreshRequestIdAfter
                << " finishEditedPointsCount=" << finishEditedJson.value("points", nlohmann::json::array()).size()
                << " finishPreservedPointsCount=" << finishPreservedAuthored.value("points", nlohmann::json::array()).size()
                << " rgbManual=" << CountPixelsWithNonZeroRgb(outputManual)
                << " rgbA1=" << CountPixelsWithNonZeroRgb(outputA1)
                << " rgbA2=" << CountPixelsWithNonZeroRgb(outputA2)
                << " rgbB=" << CountPixelsWithNonZeroRgb(outputB)
                << " alphaManual=" << CountPixelsWithNonZeroAlpha(outputManual)
                << " alphaA1=" << CountPixelsWithNonZeroAlpha(outputA1)
                << " alphaA2=" << CountPixelsWithNonZeroAlpha(outputA2)
                << " alphaB=" << CountPixelsWithNonZeroAlpha(outputB)
                << " maxRgbManual=" << maxRgbManual
                << " maxRgbA1=" << maxRgbA1
                << " maxRgbA2=" << maxRgbA2
                << " maxRgbB=" << maxRgbB
                << " authoredMiddleGrey=" << feedbackA1.authoredLayerJson.value("middleGrey", -1.0f)
                << " authoredLogMinEv=" << feedbackA1.authoredLayerJson.value("logMinEv", 999.0f)
                << " authoredLogMaxEv=" << feedbackA1.authoredLayerJson.value("logMaxEv", 999.0f)
                << " authoredEdgeProtectionB=" << authoredEdgeProtectionB
                << " authoredRangeProtectionB=" << authoredRangeProtectionB
                << " authoredFoundationShadows=" << feedbackA1.authoredLayerJson.value("foundationShadows", 999.0f)
                << " authoredFoundationDarks=" << feedbackA1.authoredLayerJson.value("foundationDarks", 999.0f)
                << " authoredFoundationMidtones=" << feedbackA1.authoredLayerJson.value("foundationMidtones", 999.0f)
                << " authoredFoundationLights=" << feedbackA1.authoredLayerJson.value("foundationLights", 999.0f)
                << " authoredFoundationHighlights=" << feedbackA1.authoredLayerJson.value("foundationHighlights", 999.0f)
                << "\n";
        } else {
            std::cout << "Tone Curve auto integration validation passed." << std::endl;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return success;
}

enum class SyntheticRawScene {
    Balanced,
    DarkMid,
    HighlightHeavy,
    NoisyLowLight
};

Raw::RawMetadata BuildSyntheticRawMetadata(int width, int height) {
    Raw::RawMetadata metadata;
    metadata.sourcePath = "synthetic-develop-smoke";
    metadata.cameraMake = "Stack";
    metadata.cameraModel = "Synthetic Bayer";
    metadata.rawWidth = width;
    metadata.rawHeight = height;
    metadata.visibleWidth = width;
    metadata.visibleHeight = height;
    metadata.bitDepth = 14;
    metadata.cfaPattern = Raw::CfaPattern::RGGB;
    metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    metadata.mosaiced = true;
    metadata.isDng = true;
    metadata.blackLevel = 512.0f;
    metadata.whiteLevel = 16383.0f;
    metadata.rawMinimum = metadata.blackLevel;
    metadata.rawMaximum = metadata.whiteLevel;
    metadata.cameraWhiteBalance = { 1.0f, 1.0f, 1.0f, 1.0f };
    metadata.daylightWhiteBalance = { 1.0f, 1.0f, 1.0f, 1.0f };
    metadata.hasDngForwardMatrix1 = true;
    metadata.hasDngBaselineExposure = true;
    metadata.dngBaselineExposure = 0.0f;
    return metadata;
}

Raw::RawImageData BuildSyntheticRawScene(SyntheticRawScene scene, int width, int height) {
    Raw::RawImageData raw;
    raw.metadata = BuildSyntheticRawMetadata(width, height);
    raw.rawBuffer.resize(static_cast<std::size_t>(width * height), 0);

    const float black = raw.metadata.blackLevel;
    const float white = raw.metadata.whiteLevel;
    const float range = white - black;
    float observedMax = black;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / (std::max)(1, width - 1);
            const float v = static_cast<float>(y) / (std::max)(1, height - 1);
            float luma = 0.0f;
            switch (scene) {
                case SyntheticRawScene::DarkMid:
                    luma = 0.035f + 0.13f * u + 0.08f * v;
                    break;
                case SyntheticRawScene::HighlightHeavy: {
                    const float dx = u - 0.72f;
                    const float dy = v - 0.34f;
                    const float spot = std::exp(-(dx * dx + dy * dy) / 0.010f);
                    luma = 0.08f + 0.22f * u + 0.12f * v + 0.92f * spot;
                    break;
                }
                case SyntheticRawScene::NoisyLowLight: {
                    const float noiseSeed = std::sin(static_cast<float>(x) * 12.9898f + static_cast<float>(y) * 78.233f) * 43758.5453f;
                    const float noise = noiseSeed - std::floor(noiseSeed);
                    const float warmPatch = (u > 0.62f && v < 0.42f) ? 0.08f : 0.0f;
                    const float texture = (std::sin(u * 34.0f) * std::cos(v * 25.0f) * 0.5f + 0.5f) * 0.025f;
                    luma = 0.012f + 0.055f * u + 0.038f * v + warmPatch + texture + (noise - 0.5f) * 0.020f;
                    break;
                }
                case SyntheticRawScene::Balanced:
                default:
                    luma = 0.16f + 0.46f * u + 0.18f * v;
                    break;
            }

            const bool red = (y % 2 == 0) && (x % 2 == 0);
            const bool blue = (y % 2 == 1) && (x % 2 == 1);
            const float channelScale = red ? 1.06f : (blue ? 0.94f : 1.0f);
            const float sample = std::clamp(black + range * luma * channelScale, black, white);
            observedMax = (std::max)(observedMax, sample);
            raw.rawBuffer[static_cast<std::size_t>(y * width + x)] =
                static_cast<std::uint16_t>(std::lround(sample));
        }
    }

    raw.metadata.rawMaximum = observedMax;
    return raw;
}

EditorNodeGraph::RawDevelopPayload BuildDevelopSmokeAutoPayload(
    float shadow,
    float midtone,
    float highlight,
    float clipping,
    float noise,
    float highlightPressure,
    float hdrSpreadEv,
    int profile,
    float recommendedBaseEv) {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    payload.integratedToneLayerJson = ToneCurveLayer().Serialize();
    payload.integratedToneLayerJson["autoSceneStatsValid"] = true;
    payload.integratedToneLayerJson["autoSceneShadowPercentile"] = shadow;
    payload.integratedToneLayerJson["autoSceneMidtonePercentile"] = midtone;
    payload.integratedToneLayerJson["autoSceneHighlightPercentile"] = highlight;
    payload.integratedToneLayerJson["autoSceneClippingRatio"] = clipping;
    payload.integratedToneLayerJson["autoSceneNoiseRisk"] = noise;
    payload.integratedToneLayerJson["autoSceneHighlightPressure"] = highlightPressure;
    payload.integratedToneLayerJson["autoSceneTextureConfidence"] = 0.70f;
    payload.integratedToneLayerJson["autoSceneHdrSpreadEv"] = hdrSpreadEv;
    payload.integratedToneLayerJson["autoSceneProfile"] = profile;
    payload.integratedToneLayerJson["autoRecommendedBaseEv"] = recommendedBaseEv;
    payload.integratedToneLayerJson["autoRecommendedLocalStrength"] = 1.08f;
    payload.integratedToneLayerJson["autoRecommendedShadowOpening"] = 1.16f;
    payload.integratedToneLayerJson["autoRecommendedHighlightCompression"] = 1.12f;
    payload.autoGuidance.autoStrength = 1.10f;
    payload.autoGuidance.dynamicRange = 1.15f;
    return payload;
}

bool ValidateDevelopGraphStateSerialization() {
    ToneCurveLayer layer;
    nlohmann::json graphJson = layer.Serialize();
    graphJson["activeGraphView"] = 1;
    graphJson["preparedPoints"] = nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f }, { "shape", 0 } },
        { { "x", 0.5f }, { "y", 0.58f }, { "shape", 1 } },
        { { "x", 1.0f }, { "y", 1.0f }, { "shape", 0 } }
    });
    graphJson["points"] = nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f }, { "shape", 0 } },
        { { "x", 0.5f }, { "y", 0.47f }, { "shape", 2 } },
        { { "x", 1.0f }, { "y", 1.0f }, { "shape", 0 } }
    });

    ToneCurveLayer restored;
    restored.Deserialize(graphJson);
    const nlohmann::json roundTrip = restored.Serialize();
    const bool graphViewPreserved = roundTrip.value("activeGraphView", -1) == 1;
    const bool preparedPointsPreserved =
        roundTrip.contains("preparedPoints") &&
        roundTrip["preparedPoints"].is_array() &&
        roundTrip["preparedPoints"].size() == 3 &&
        std::abs(roundTrip["preparedPoints"][1].value("y", 0.0f) - 0.58f) < 0.0001f;
    const bool finalPointsPreserved =
        roundTrip.contains("points") &&
        roundTrip["points"].is_array() &&
        roundTrip["points"].size() == 3 &&
        std::abs(roundTrip["points"][1].value("y", 0.0f) - 0.47f) < 0.0001f;
    const bool success = graphViewPreserved && preparedPointsPreserved && finalPointsPreserved;
    if (!success) {
        std::cerr
            << "Develop graph state validation failed:"
            << " graphViewPreserved=" << graphViewPreserved
            << " preparedPointsPreserved=" << preparedPointsPreserved
            << " finalPointsPreserved=" << finalPointsPreserved
            << " activeGraphView=" << roundTrip.value("activeGraphView", -1)
            << "\n";
    }
    return success;
}

bool ValidateDevelopAutoIntentSerialization() {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    payload.autoGuidance.subjectSceneBias = 0.62f;
    payload.autoGuidance.moodReadabilityBias = -0.35f;
    payload.subjectImportance.enabled = true;
    payload.subjectImportance.showOverlay = true;
    payload.subjectImportance.overlayOpacity = 0.52f;
    payload.subjectImportance.showInterpretedMapOverlay = true;
    payload.subjectImportance.interpretedMapOpacity = 0.37f;
    payload.subjectImportance.showRefinedMapOverlay = true;
    payload.subjectImportance.refinedMapOpacity = 0.43f;
    payload.subjectImportance.brushEnabled = true;
    payload.subjectImportance.brushSubtract = false;
    payload.subjectImportance.brushMode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    payload.subjectImportance.brushRadius = 0.064f;
    payload.subjectImportance.brushFeather = 0.47f;
    payload.subjectImportance.brushStrength = 0.71f;
    payload.subjectImportance.activeRegionId = 3;
    payload.subjectImportance.activeStrokeId = 5;
    payload.subjectImportance.nextRegionId = 4;
    payload.subjectImportance.nextStrokeId = 6;
    EditorNodeGraph::DevelopSubjectImportanceRegion importanceRegion;
    importanceRegion.id = 3;
    importanceRegion.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Protect;
    importanceRegion.enabled = true;
    importanceRegion.centerX = 0.42f;
    importanceRegion.centerY = 0.58f;
    importanceRegion.radiusX = 0.24f;
    importanceRegion.radiusY = 0.18f;
    importanceRegion.feather = 0.44f;
    importanceRegion.strength = 0.83f;
    payload.subjectImportance.regions.push_back(importanceRegion);
    EditorNodeGraph::DevelopSubjectImportanceStroke importanceStroke;
    importanceStroke.id = 5;
    importanceStroke.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    importanceStroke.enabled = true;
    importanceStroke.subtract = false;
    importanceStroke.radius = 0.064f;
    importanceStroke.feather = 0.47f;
    importanceStroke.strength = 0.71f;
    importanceStroke.points.push_back({ 0.34f, 0.43f });
    importanceStroke.points.push_back({ 0.41f, 0.49f });
    importanceStroke.points.push_back({ 0.52f, 0.57f });
    payload.subjectImportance.strokes.push_back(importanceStroke);
    payload.integratedToneLayerJson = ToneCurveLayer().Serialize();

    EditorNodeGraph::Graph graph;
    EditorNodeGraph::Node* node = graph.AddRawDevelopNode(payload, EditorNodeGraph::Vec2{ 10.0f, 20.0f });
    const int developNodeId = node ? node->id : 0;
    const nlohmann::json serialized = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);
    const nlohmann::json nodesJson = serialized.value("nodeGraph", nlohmann::json::object()).value("nodes", nlohmann::json::array());
    std::string serializedIntent;
    nlohmann::json developNodeJson;
    for (const nlohmann::json& item : nodesJson) {
        if (item.value("id", 0) == developNodeId) {
            developNodeJson = item;
            serializedIntent = item.value("developAutoGuidance", nlohmann::json::object())
                .value("autoIntent", std::string());
            break;
        }
    }

    EditorNodeGraph::Graph restoredGraph;
    EditorNodeGraph::DeserializeGraphPayload(serialized, restoredGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* restoredNode = restoredGraph.FindNode(developNodeId);
    const bool roundTripPreserved =
        restoredNode &&
        restoredNode->rawDevelop.autoGuidance.intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast &&
        std::abs(restoredNode->rawDevelop.autoGuidance.subjectSceneBias - payload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(restoredNode->rawDevelop.autoGuidance.moodReadabilityBias - payload.autoGuidance.moodReadabilityBias) < 0.0001f &&
        restoredNode->rawDevelop.subjectImportance.enabled &&
        restoredNode->rawDevelop.subjectImportance.showOverlay &&
        restoredNode->rawDevelop.subjectImportance.showInterpretedMapOverlay &&
        restoredNode->rawDevelop.subjectImportance.showRefinedMapOverlay &&
        restoredNode->rawDevelop.subjectImportance.brushEnabled &&
        restoredNode->rawDevelop.subjectImportance.brushMode == EditorNodeGraph::DevelopSubjectImportanceMode::Reveal &&
        restoredNode->rawDevelop.subjectImportance.activeRegionId == payload.subjectImportance.activeRegionId &&
        restoredNode->rawDevelop.subjectImportance.activeStrokeId == payload.subjectImportance.activeStrokeId &&
        restoredNode->rawDevelop.subjectImportance.regions.size() == 1 &&
        restoredNode->rawDevelop.subjectImportance.strokes.size() == 1 &&
        restoredNode->rawDevelop.subjectImportance.regions[0].mode == EditorNodeGraph::DevelopSubjectImportanceMode::Protect &&
        restoredNode->rawDevelop.subjectImportance.strokes[0].mode == EditorNodeGraph::DevelopSubjectImportanceMode::Reveal &&
        std::abs(restoredNode->rawDevelop.subjectImportance.overlayOpacity - payload.subjectImportance.overlayOpacity) < 0.0001f &&
        std::abs(restoredNode->rawDevelop.subjectImportance.interpretedMapOpacity - payload.subjectImportance.interpretedMapOpacity) < 0.0001f &&
        std::abs(restoredNode->rawDevelop.subjectImportance.refinedMapOpacity - payload.subjectImportance.refinedMapOpacity) < 0.0001f &&
        std::abs(restoredNode->rawDevelop.subjectImportance.brushRadius - payload.subjectImportance.brushRadius) < 0.0001f &&
        std::abs(restoredNode->rawDevelop.subjectImportance.regions[0].centerX - importanceRegion.centerX) < 0.0001f &&
        std::abs(restoredNode->rawDevelop.subjectImportance.regions[0].strength - importanceRegion.strength) < 0.0001f &&
        restoredNode->rawDevelop.subjectImportance.strokes[0].points.size() == 3 &&
        std::abs(restoredNode->rawDevelop.subjectImportance.strokes[0].points[1].x - 0.41f) < 0.0001f;
    const bool serializedUserIntentAxes =
        std::abs(developNodeJson.value("developAutoGuidance", nlohmann::json::object())
            .value("subjectSceneBias", -99.0f) - payload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(developNodeJson.value("developAutoGuidance", nlohmann::json::object())
            .value("moodReadabilityBias", -99.0f) - payload.autoGuidance.moodReadabilityBias) < 0.0001f;
    const nlohmann::json serializedImportance =
        developNodeJson.value("developSubjectImportance", nlohmann::json::object());
    const nlohmann::json serializedImportanceRegions =
        serializedImportance.value("regions", nlohmann::json::array());
    const nlohmann::json serializedImportanceStrokes =
        serializedImportance.value("strokes", nlohmann::json::array());
    const bool serializedSubjectImportance =
        serializedImportance.value("enabled", false) &&
        serializedImportance.value("showOverlay", false) &&
        serializedImportance.value("showInterpretedMapOverlay", false) &&
        serializedImportance.value("showRefinedMapOverlay", false) &&
        serializedImportance.value("brushEnabled", false) &&
        serializedImportance.value("brushMode", std::string()) == "Reveal" &&
        serializedImportance.value("activeRegionId", 0) == payload.subjectImportance.activeRegionId &&
        serializedImportance.value("activeStrokeId", 0) == payload.subjectImportance.activeStrokeId &&
        std::abs(serializedImportance.value("overlayOpacity", -1.0f) -
            payload.subjectImportance.overlayOpacity) < 0.0001f &&
        std::abs(serializedImportance.value("interpretedMapOpacity", -1.0f) -
            payload.subjectImportance.interpretedMapOpacity) < 0.0001f &&
        std::abs(serializedImportance.value("refinedMapOpacity", -1.0f) -
            payload.subjectImportance.refinedMapOpacity) < 0.0001f &&
        std::abs(serializedImportance.value("brushRadius", -1.0f) -
            payload.subjectImportance.brushRadius) < 0.0001f &&
        serializedImportanceRegions.is_array() &&
        serializedImportanceRegions.size() == 1 &&
        serializedImportanceRegions[0].value("mode", std::string()) == "Protect" &&
        std::abs(serializedImportanceRegions[0].value("centerX", -1.0f) -
            importanceRegion.centerX) < 0.0001f &&
        std::abs(serializedImportanceRegions[0].value("strength", -1.0f) -
            importanceRegion.strength) < 0.0001f &&
        serializedImportanceStrokes.is_array() &&
        serializedImportanceStrokes.size() == 1 &&
        serializedImportanceStrokes[0].value("mode", std::string()) == "Reveal" &&
        serializedImportanceStrokes[0].value("points", nlohmann::json::array()).is_array() &&
        serializedImportanceStrokes[0].value("points", nlohmann::json::array()).size() == 3 &&
        std::abs(serializedImportanceStrokes[0].value("points", nlohmann::json::array())[1].value("x", -1.0f) - 0.41f) < 0.0001f;

    nlohmann::json legacySerialized = serialized;
    for (nlohmann::json& item : legacySerialized["nodeGraph"]["nodes"]) {
        if (item.value("id", 0) == developNodeId) {
            item["developAutoGuidance"].erase("autoIntent");
            item["developAutoGuidance"].erase("subjectSceneBias");
            item["developAutoGuidance"].erase("moodReadabilityBias");
            item.erase("developSubjectImportance");
            break;
        }
    }
    EditorNodeGraph::Graph legacyGraph;
    EditorNodeGraph::DeserializeGraphPayload(legacySerialized, legacyGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* legacyNode = legacyGraph.FindNode(developNodeId);
    const bool legacyDefaultsToNatural =
        legacyNode &&
        legacyNode->rawDevelop.autoGuidance.intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished &&
        std::abs(legacyNode->rawDevelop.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(legacyNode->rawDevelop.autoGuidance.moodReadabilityBias) < 0.0001f &&
        !legacyNode->rawDevelop.subjectImportance.enabled &&
        !legacyNode->rawDevelop.subjectImportance.showInterpretedMapOverlay &&
        !legacyNode->rawDevelop.subjectImportance.showRefinedMapOverlay &&
        legacyNode->rawDevelop.subjectImportance.activeRegionId == 0 &&
        legacyNode->rawDevelop.subjectImportance.activeStrokeId == 0 &&
        legacyNode->rawDevelop.subjectImportance.regions.empty() &&
        legacyNode->rawDevelop.subjectImportance.strokes.empty();

    nlohmann::json unknownSerialized = serialized;
    for (nlohmann::json& item : unknownSerialized["nodeGraph"]["nodes"]) {
        if (item.value("id", 0) == developNodeId) {
            item["developAutoGuidance"]["autoIntent"] = "DefinitelyNotADevelopIntent";
            item["developSubjectImportance"]["brushMode"] = "DefinitelyNotABrushMode";
            item["developSubjectImportance"]["regions"][0]["mode"] = "DefinitelyNotARegionMode";
            item["developSubjectImportance"]["strokes"][0]["mode"] = "DefinitelyNotAStrokeMode";
            break;
        }
    }
    EditorNodeGraph::Graph unknownGraph;
    EditorNodeGraph::DeserializeGraphPayload(unknownSerialized, unknownGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* unknownNode = unknownGraph.FindNode(developNodeId);
    const bool unknownDefaultsToNatural =
        unknownNode &&
        unknownNode->rawDevelop.autoGuidance.intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished &&
        std::abs(unknownNode->rawDevelop.autoGuidance.subjectSceneBias - payload.autoGuidance.subjectSceneBias) < 0.0001f &&
        std::abs(unknownNode->rawDevelop.autoGuidance.moodReadabilityBias - payload.autoGuidance.moodReadabilityBias) < 0.0001f &&
        unknownNode->rawDevelop.subjectImportance.activeRegionId == payload.subjectImportance.activeRegionId &&
        unknownNode->rawDevelop.subjectImportance.activeStrokeId == payload.subjectImportance.activeStrokeId &&
        unknownNode->rawDevelop.subjectImportance.brushMode == EditorNodeGraph::DevelopSubjectImportanceMode::Important &&
        unknownNode->rawDevelop.subjectImportance.regions.size() == 1 &&
        unknownNode->rawDevelop.subjectImportance.regions[0].mode == EditorNodeGraph::DevelopSubjectImportanceMode::Important &&
        unknownNode->rawDevelop.subjectImportance.strokes.size() == 1 &&
        unknownNode->rawDevelop.subjectImportance.strokes[0].mode == EditorNodeGraph::DevelopSubjectImportanceMode::Important;

    EditorModule viewportModule;
    EditorNodeGraph::RawDevelopPayload viewportPayload = payload;
    viewportPayload.subjectImportance.enabled = true;
    viewportPayload.subjectImportance.showOverlay = true;
    viewportPayload.subjectImportance.showInterpretedMapOverlay = true;
    viewportPayload.subjectImportance.showRefinedMapOverlay = true;
    EditorNodeGraph::Node* viewportNode =
        viewportModule.GetNodeGraph().AddRawDevelopNode(viewportPayload, EditorNodeGraph::Vec2{ 0.0f, 0.0f });
    if (viewportNode) {
        viewportModule.GetNodeGraph().SelectNode(viewportNode->id);
    }
    EditorModule::DevelopSubjectViewportState viewportState;
    const bool interpretedMapViewportState =
        viewportNode &&
        viewportModule.GetDevelopSubjectImportanceViewportState(viewportState) &&
        viewportState.showInterpretedMapOverlay &&
        viewportState.interpretedMapActive &&
        viewportState.interpretedMapGridWidth == 5 &&
        viewportState.interpretedMapGridHeight == 5 &&
        viewportState.interpretedMapCells.size() == 25 &&
        std::abs(viewportState.interpretedMapOpacity - payload.subjectImportance.interpretedMapOpacity) < 0.0001f &&
        viewportState.showRefinedMapOverlay &&
        viewportState.refinedMapActive &&
        viewportState.refinedMapGridWidth == 5 &&
        viewportState.refinedMapGridHeight == 5 &&
        viewportState.refinedMapCells.size() == 25 &&
        std::abs(viewportState.refinedMapOpacity - payload.subjectImportance.refinedMapOpacity) < 0.0001f;

    const bool success =
        serializedIntent == "PunchyHighContrast" &&
        serializedUserIntentAxes &&
        serializedSubjectImportance &&
        roundTripPreserved &&
        legacyDefaultsToNatural &&
        unknownDefaultsToNatural &&
        interpretedMapViewportState &&
        !developNodeJson.empty();
    if (!success) {
        std::cerr
            << "Develop auto intent serialization validation failed:"
            << " serializedIntent=" << serializedIntent
            << " serializedUserIntentAxes=" << serializedUserIntentAxes
            << " serializedSubjectImportance=" << serializedSubjectImportance
            << " roundTripPreserved=" << roundTripPreserved
            << " legacyDefaultsToNatural=" << legacyDefaultsToNatural
            << " unknownDefaultsToNatural=" << unknownDefaultsToNatural
            << " interpretedMapViewportState=" << interpretedMapViewportState
            << " developNodeFound=" << !developNodeJson.empty()
            << "\n";
    }
    return success;
}

bool ValidateDevelopNodeSmoke() {
    constexpr int kRawWidth = 96;
    constexpr int kRawHeight = 64;

    if (!ValidateDevelopAutoSolveBehavior()) {
        return false;
    }
    if (!ValidateDevelopGraphStateSerialization()) {
        return false;
    }
    if (!ValidateDevelopAutoIntentSerialization()) {
        return false;
    }

    const Raw::RawMetadata metadata = BuildSyntheticRawMetadata(kRawWidth, kRawHeight);
    EditorNodeGraph::RawDevelopPayload neutralPayload = BuildDevelopSmokeAutoPayload(
        0.05f, 0.19f, 0.74f, 0.000f, 0.14f, 0.12f, 2.60f, 0, 0.02f);
    EditorNodeGraph::RawDevelopPayload biasedPayload = neutralPayload;
    biasedPayload.autoGuidance.exposureBias = 0.55f;
    EditorModule::ApplyDevelopAutoSolve(neutralPayload, metadata, true);
    EditorModule::ApplyDevelopAutoSolve(biasedPayload, metadata, true);

    EditorNodeGraph::RawDevelopPayload stablePayload = neutralPayload;
    const Raw::RawDevelopSettings stableSettingsBefore = stablePayload.settings;
    const Raw::RawDetailFusionSettings stablePrepBefore = stablePayload.scenePrepSettings;
    EditorModule::ApplyDevelopAutoSolve(stablePayload, metadata, true);
    const bool repeatedSolveStable =
        std::abs(stablePayload.settings.exposureStops - stableSettingsBefore.exposureStops) < 0.0001f &&
        stablePayload.settings.highlightMode == stableSettingsBefore.highlightMode &&
        std::abs(stablePayload.settings.highlightStrength - stableSettingsBefore.highlightStrength) < 0.0001f &&
        std::abs(stablePayload.scenePrepSettings.baseEvBias - stablePrepBefore.baseEvBias) < 0.0001f;
    const bool positiveBiasBrightens =
        biasedPayload.settings.exposureStops > neutralPayload.settings.exposureStops + 0.65f;

    EditorNodeGraph::RawDevelopPayload highlightPayload = BuildDevelopSmokeAutoPayload(
        0.02f, 0.11f, 0.97f, 0.022f, 0.20f, 0.86f, 5.80f, 1, 0.36f);
    EditorModule::ApplyDevelopAutoSolve(highlightPayload, metadata, true);
    const bool highlightSolveProtects =
        highlightPayload.settings.highlightMode == Raw::HighlightReconstructionMode::ColorReconstruction &&
        highlightPayload.settings.highlightStrength > neutralPayload.settings.highlightStrength + 0.08f &&
        highlightPayload.scenePrepSettings.highlightProtectionBias > neutralPayload.scenePrepSettings.highlightProtectionBias + 0.18f;

    EditorNodeGraph::RawDevelopPayload noisyPayload = BuildDevelopSmokeAutoPayload(
        0.010f, 0.090f, 0.62f, 0.000f, 0.88f, 0.16f, 2.60f, 4, 0.92f);
    noisyPayload.autoGuidance.dynamicRange = 1.10f;
    noisyPayload.autoGuidance.shadowLift = 0.32f;
    EditorModule::ApplyDevelopAutoSolve(noisyPayload, metadata, true);
    EditorNodeGraph::RawDevelopPayload noisyToneOnlyPayload = noisyPayload;
    noisyToneOnlyPayload.scenePrepEnabled = false;

    if (!glfwInit()) {
        std::cerr << "Develop smoke validation failed: glfwInit() failed.\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Develop Smoke Validation", nullptr, nullptr);
    if (!window) {
        std::cerr << "Develop smoke validation failed: unable to create hidden OpenGL window.\n";
        glfwTerminate();
        return false;
    }

    bool renderSuccess = false;
    bool balancedNonBlank = false;
    bool darkNonBlank = false;
    bool highlightNonBlank = false;
    bool demosaicBilinearStable = false;
    bool manualOrientationNonBlank = false;
    bool developGraphBalancedNonBlank = false;
    bool developGraphDarkNonBlank = false;
    bool developGraphHighlightNonBlank = false;
    bool developGraphNoisyNonBlank = false;
    bool developGraphNoisyToneOnlyNonBlank = false;
    bool developGraphDngCalibrationNonBlank = false;
    bool developGraphRawStageCacheReuseObserved = false;
    bool developGraphPreFinishStageCacheReuseObserved = false;
    bool noisyCombinedToneNotCollapsed = false;
    float balancedMaxRgb = 0.0f;
    float darkMaxRgb = 0.0f;
    float highlightMaxRgb = 0.0f;
    float manualOrientationMaxRgb = 0.0f;
    float developGraphBalancedMaxRgb = 0.0f;
    float developGraphDarkMaxRgb = 0.0f;
    float developGraphHighlightMaxRgb = 0.0f;
    float developGraphNoisyMaxRgb = 0.0f;
    float developGraphNoisyToneOnlyMaxRgb = 0.0f;
    float developGraphDngCalibrationMaxRgb = 0.0f;
    float developGraphNoisyAvgLuma = 0.0f;
    float developGraphNoisyToneOnlyAvgLuma = 0.0f;

    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Develop smoke validation failed: unable to load OpenGL functions.\n";
    } else {
        Raw::RawGpuPipeline rawPipeline;
        const Raw::RawImageData balancedRaw = BuildSyntheticRawScene(SyntheticRawScene::Balanced, kRawWidth, kRawHeight);
        const Raw::RawImageData darkRaw = BuildSyntheticRawScene(SyntheticRawScene::DarkMid, kRawWidth, kRawHeight);
        const Raw::RawImageData highlightRaw = BuildSyntheticRawScene(SyntheticRawScene::HighlightHeavy, kRawWidth, kRawHeight);
        const Raw::RawImageData noisyRaw = BuildSyntheticRawScene(SyntheticRawScene::NoisyLowLight, kRawWidth, kRawHeight);
        Raw::RawImageData dngCalibrationRaw = balancedRaw;
        dngCalibrationRaw.metadata.hasDngAsShotNeutral = true;
        dngCalibrationRaw.metadata.dngAsShotNeutral = { 0.86f, 1.0f, 0.46f };
        dngCalibrationRaw.metadata.cameraWhiteBalance = { 1.0f / 0.86f, 1.0f, 1.0f / 0.46f, 1.0f };
        dngCalibrationRaw.metadata.hasDngForwardMatrix2 = true;
        dngCalibrationRaw.metadata.dngForwardMatrix1 = {
            0.4360747f, 0.3850649f, 0.1430804f,
            0.2225045f, 0.7168786f, 0.0606169f,
            0.0139322f, 0.0971045f, 0.7141733f
        };
        dngCalibrationRaw.metadata.dngForwardMatrix2 = {
            0.4560747f, 0.3650649f, 0.1430804f,
            0.2325045f, 0.7068786f, 0.0606169f,
            0.0139322f, 0.0871045f, 0.7241733f
        };
        dngCalibrationRaw.metadata.hasDngAnalogBalance = true;
        dngCalibrationRaw.metadata.dngAnalogBalance = { 1.05f, 1.0f, 0.96f };
        dngCalibrationRaw.metadata.hasDngCameraCalibration1 = true;
        dngCalibrationRaw.metadata.hasDngCameraCalibration2 = true;
        dngCalibrationRaw.metadata.dngCameraCalibration1 = {
            1.02f, 0.01f, 0.00f,
            0.00f, 1.00f, 0.00f,
            0.00f, 0.02f, 0.98f
        };
        dngCalibrationRaw.metadata.dngCameraCalibration2 = {
            0.98f, 0.02f, 0.00f,
            0.00f, 1.01f, 0.00f,
            0.00f, 0.01f, 1.03f
        };

        Raw::RawDevelopSettings bilinearSettings = neutralPayload.settings;
        bilinearSettings.cameraTransformEnabled = false;
        bilinearSettings.demosaicMethod = Raw::DemosaicMethod::Bilinear;
        unsigned int balancedTexture = rawPipeline.Render(balancedRaw, bilinearSettings);
        const int balancedW = rawPipeline.GetOutputWidth();
        const int balancedH = rawPipeline.GetOutputHeight();
        const std::vector<float> bilinearPixels = ReadTextureRgbaFloat(balancedTexture, balancedW, balancedH);
        balancedMaxRgb = ReadTextureMaxRgb(balancedTexture, balancedW, balancedH);
        balancedNonBlank = balancedTexture != 0 && balancedW == kRawWidth && balancedH == kRawHeight && balancedMaxRgb > 0.01f;
        demosaicBilinearStable = balancedNonBlank && !bilinearPixels.empty();

        Raw::RawDevelopSettings darkSettings = biasedPayload.settings;
        darkSettings.cameraTransformEnabled = false;
        darkSettings.demosaicMethod = Raw::DemosaicMethod::Bilinear;
        unsigned int darkTexture = rawPipeline.Render(darkRaw, darkSettings);
        darkMaxRgb = ReadTextureMaxRgb(darkTexture, rawPipeline.GetOutputWidth(), rawPipeline.GetOutputHeight());
        darkNonBlank = darkTexture != 0 && darkMaxRgb > 0.01f;

        Raw::RawDevelopSettings highlightSettings = highlightPayload.settings;
        highlightSettings.cameraTransformEnabled = false;
        highlightSettings.demosaicMethod = Raw::DemosaicMethod::Bilinear;
        unsigned int highlightTexture = rawPipeline.Render(highlightRaw, highlightSettings);
        highlightMaxRgb = ReadTextureMaxRgb(highlightTexture, rawPipeline.GetOutputWidth(), rawPipeline.GetOutputHeight());
        highlightNonBlank = highlightTexture != 0 && highlightMaxRgb > 0.01f;

        Raw::RawDevelopSettings orientationSettings = bilinearSettings;
        orientationSettings.rotationDegrees = 90;
        unsigned int orientationTexture = rawPipeline.Render(balancedRaw, orientationSettings);
        const int orientationW = rawPipeline.GetOutputWidth();
        const int orientationH = rawPipeline.GetOutputHeight();
        manualOrientationMaxRgb = ReadTextureMaxRgb(orientationTexture, orientationW, orientationH);
        manualOrientationNonBlank =
            orientationTexture != 0 &&
            orientationW == kRawHeight &&
            orientationH == kRawWidth &&
            manualOrientationMaxRgb > 0.01f;

        RenderPipeline graphPipeline;
        graphPipeline.Initialize();
        graphPipeline.Resize(kRawWidth, kRawHeight);
        auto runDevelopGraph = [&](const Raw::RawImageData& raw,
                                   const EditorNodeGraph::RawDevelopPayload& payload,
                                   std::uint64_t requestRevision,
                                   float& outMaxRgb,
                                   float* outAvgLuma,
                                   bool* outRawBaseCacheHit = nullptr,
                                   bool* outPreFinishCacheHit = nullptr) {
            RenderGraphSnapshot graph;
            graph.outputNodeId = 3;

            RenderGraphNode rawSourceNode;
            rawSourceNode.nodeId = 1;
            rawSourceNode.kind = RenderGraphNodeKind::RawSource;
            rawSourceNode.requestRevision = requestRevision;
            rawSourceNode.rawSource.metadata = raw.metadata;
            rawSourceNode.rawSource.embeddedRawData = raw;
            graph.nodes.push_back(std::move(rawSourceNode));

            RenderGraphNode developNode;
            developNode.nodeId = 2;
            developNode.kind = RenderGraphNodeKind::RawDevelop;
            developNode.requestRevision = requestRevision;
            developNode.rawDevelop.settings = payload.settings;
            developNode.rawDevelop.scenePrepEnabled = payload.scenePrepEnabled;
            developNode.rawDevelop.scenePrepSettings = payload.scenePrepSettings;
            developNode.rawDevelop.integratedToneEnabled = payload.integratedToneEnabled;
            developNode.rawDevelop.integratedToneLayerJson = payload.integratedToneLayerJson;
            graph.nodes.push_back(std::move(developNode));

            RenderGraphNode outputNode;
            outputNode.nodeId = 3;
            outputNode.kind = RenderGraphNodeKind::Output;
            outputNode.requestRevision = requestRevision;
            graph.nodes.push_back(std::move(outputNode));

            graph.links.push_back(RenderGraphLink{ 1, "rawOut", 2, "rawIn" });
            graph.links.push_back(RenderGraphLink{ 2, "imageOut", 3, "imageIn" });

            graphPipeline.Resize(kRawWidth, kRawHeight);
            graphPipeline.ExecuteGraph(graph);
            if (outRawBaseCacheHit) {
                *outRawBaseCacheHit = graphPipeline.WasGraphImageCacheHit(2, "__rawDevelopBase");
            }
            if (outPreFinishCacheHit) {
                *outPreFinishCacheHit = graphPipeline.WasGraphImageCacheHit(
                    2,
                    EditorNodeGraph::kPreFinishImageOutputSocketId);
            }
            outMaxRgb = ReadTextureMaxRgb(
                graphPipeline.GetOutputTexture(),
                graphPipeline.GetCanvasWidth(),
                graphPipeline.GetCanvasHeight());
            int outputW = 0;
            int outputH = 0;
            const std::vector<unsigned char> outputPixels = graphPipeline.GetOutputPixels(outputW, outputH);
            if (outAvgLuma) {
                *outAvgLuma = ComputeAverageNormalizedLuma(outputPixels);
            }
            return graphPipeline.GetOutputTexture() != 0 &&
                outputW == kRawWidth &&
                outputH == kRawHeight &&
                !outputPixels.empty() &&
                outMaxRgb > 0.01f;
        };
        developGraphBalancedNonBlank =
            runDevelopGraph(balancedRaw, neutralPayload, 101, developGraphBalancedMaxRgb, nullptr);
        developGraphDarkNonBlank =
            runDevelopGraph(darkRaw, biasedPayload, 102, developGraphDarkMaxRgb, nullptr);
        developGraphHighlightNonBlank =
            runDevelopGraph(highlightRaw, highlightPayload, 103, developGraphHighlightMaxRgb, nullptr);
        developGraphNoisyNonBlank =
            runDevelopGraph(noisyRaw, noisyPayload, 104, developGraphNoisyMaxRgb, &developGraphNoisyAvgLuma);
        developGraphNoisyToneOnlyNonBlank =
            runDevelopGraph(noisyRaw, noisyToneOnlyPayload, 105, developGraphNoisyToneOnlyMaxRgb, &developGraphNoisyToneOnlyAvgLuma);
        EditorNodeGraph::RawDevelopPayload dngCalibrationPayload = neutralPayload;
        dngCalibrationPayload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
        dngCalibrationPayload.settings.cameraTransformEnabled = true;
        dngCalibrationPayload.settings.cameraTransformSource = Raw::RawCameraTransformSource::DngAuto;
        developGraphDngCalibrationNonBlank =
            runDevelopGraph(dngCalibrationRaw, dngCalibrationPayload, 106, developGraphDngCalibrationMaxRgb, nullptr);

        EditorNodeGraph::RawDevelopPayload stagePrepVariantPayload = neutralPayload;
        stagePrepVariantPayload.scenePrepSettings.strength =
            std::clamp(stagePrepVariantPayload.scenePrepSettings.strength + 0.17f, 0.0f, 1.25f);
        stagePrepVariantPayload.scenePrepSettings.maxEvBias =
            std::clamp(stagePrepVariantPayload.scenePrepSettings.maxEvBias + 0.22f, -1.25f, 1.25f);
        bool rawStageCacheHit = false;
        float stagePrepVariantMaxRgb = 0.0f;
        const bool stagePrepVariantNonBlank = runDevelopGraph(
            balancedRaw,
            stagePrepVariantPayload,
            107,
            stagePrepVariantMaxRgb,
            nullptr,
            &rawStageCacheHit,
            nullptr);

        EditorNodeGraph::RawDevelopPayload finishToneVariantPayload = neutralPayload;
        finishToneVariantPayload.integratedToneLayerJson["stageCacheValidationProbe"] = "finishToneOnly";
        bool preFinishStageCacheHit = false;
        float finishToneVariantMaxRgb = 0.0f;
        const bool finishToneVariantNonBlank = runDevelopGraph(
            balancedRaw,
            finishToneVariantPayload,
            108,
            finishToneVariantMaxRgb,
            nullptr,
            nullptr,
            &preFinishStageCacheHit);

        developGraphRawStageCacheReuseObserved =
            stagePrepVariantNonBlank &&
            stagePrepVariantMaxRgb > 0.01f &&
            rawStageCacheHit;
        developGraphPreFinishStageCacheReuseObserved =
            finishToneVariantNonBlank &&
            finishToneVariantMaxRgb > 0.01f &&
            preFinishStageCacheHit;
        noisyCombinedToneNotCollapsed =
            developGraphNoisyNonBlank &&
            developGraphNoisyToneOnlyNonBlank &&
            developGraphNoisyMaxRgb > developGraphNoisyToneOnlyMaxRgb * 0.28f &&
            developGraphNoisyAvgLuma > 0.055f;

        renderSuccess =
            balancedNonBlank &&
            darkNonBlank &&
            highlightNonBlank &&
            demosaicBilinearStable &&
            manualOrientationNonBlank &&
            developGraphBalancedNonBlank &&
            developGraphDarkNonBlank &&
            developGraphHighlightNonBlank &&
            developGraphDngCalibrationNonBlank &&
            developGraphRawStageCacheReuseObserved &&
            developGraphPreFinishStageCacheReuseObserved &&
            noisyCombinedToneNotCollapsed;
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    const bool success =
        repeatedSolveStable &&
        positiveBiasBrightens &&
        highlightSolveProtects &&
        renderSuccess;

    if (!success) {
        std::cerr
            << "Develop smoke validation failed:"
            << " repeatedSolveStable=" << repeatedSolveStable
            << " positiveBiasBrightens=" << positiveBiasBrightens
            << " highlightSolveProtects=" << highlightSolveProtects
            << " balancedNonBlank=" << balancedNonBlank
            << " darkNonBlank=" << darkNonBlank
            << " highlightNonBlank=" << highlightNonBlank
            << " demosaicBilinearStable=" << demosaicBilinearStable
            << " manualOrientationNonBlank=" << manualOrientationNonBlank
            << " developGraphBalancedNonBlank=" << developGraphBalancedNonBlank
            << " developGraphDarkNonBlank=" << developGraphDarkNonBlank
            << " developGraphHighlightNonBlank=" << developGraphHighlightNonBlank
            << " developGraphNoisyNonBlank=" << developGraphNoisyNonBlank
            << " developGraphNoisyToneOnlyNonBlank=" << developGraphNoisyToneOnlyNonBlank
            << " developGraphDngCalibrationNonBlank=" << developGraphDngCalibrationNonBlank
            << " developGraphRawStageCacheReuseObserved=" << developGraphRawStageCacheReuseObserved
            << " developGraphPreFinishStageCacheReuseObserved=" << developGraphPreFinishStageCacheReuseObserved
            << " noisyCombinedToneNotCollapsed=" << noisyCombinedToneNotCollapsed
            << " neutralExposure=" << neutralPayload.settings.exposureStops
            << " biasedExposure=" << biasedPayload.settings.exposureStops
            << " neutralHighlightStrength=" << neutralPayload.settings.highlightStrength
            << " highlightStrength=" << highlightPayload.settings.highlightStrength
            << " neutralHighlightBias=" << neutralPayload.scenePrepSettings.highlightProtectionBias
            << " highlightBias=" << highlightPayload.scenePrepSettings.highlightProtectionBias
            << " balancedMaxRgb=" << balancedMaxRgb
            << " darkMaxRgb=" << darkMaxRgb
            << " highlightMaxRgb=" << highlightMaxRgb
            << " manualOrientationMaxRgb=" << manualOrientationMaxRgb
            << " developGraphBalancedMaxRgb=" << developGraphBalancedMaxRgb
            << " developGraphDarkMaxRgb=" << developGraphDarkMaxRgb
            << " developGraphHighlightMaxRgb=" << developGraphHighlightMaxRgb
            << " developGraphNoisyMaxRgb=" << developGraphNoisyMaxRgb
            << " developGraphNoisyToneOnlyMaxRgb=" << developGraphNoisyToneOnlyMaxRgb
            << " developGraphDngCalibrationMaxRgb=" << developGraphDngCalibrationMaxRgb
            << " developGraphNoisyAvgLuma=" << developGraphNoisyAvgLuma
            << " developGraphNoisyToneOnlyAvgLuma=" << developGraphNoisyToneOnlyAvgLuma
            << "\n";
    } else {
        std::cout << "Develop node smoke validation passed." << std::endl;
    }

    return success;
}

std::filesystem::path ResolveValidationInputPath(const char* rawPath) {
    std::filesystem::path path(rawPath ? rawPath : "");
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return path;
    }
#ifdef _WIN32
    const std::filesystem::path fromWorkspace = std::filesystem::current_path(ec).parent_path() / path;
    if (!ec && std::filesystem::exists(fromWorkspace, ec)) {
        return fromWorkspace;
    }
#endif
    return path;
}

std::string SanitizeValidationFileStem(std::string value) {
    for (char& ch : value) {
        const bool keep =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' ||
            ch == '_';
        if (!keep) {
            ch = '_';
        }
    }
    return value.empty() ? std::string("raw") : value;
}

bool WriteValidationPng(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    if (path.empty() || pixels.empty() || width <= 0 || height <= 0) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::vector<unsigned char> displayPixels = pixels;
    for (std::size_t i = 0; i + 2 < displayPixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const float linear = static_cast<float>(displayPixels[i + static_cast<std::size_t>(c)]) / 255.0f;
            const float encoded = linear <= 0.0031308f
                ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            displayPixels[i + static_cast<std::size_t>(c)] =
                static_cast<unsigned char>(std::clamp(std::lround(encoded * 255.0f), 0l, 255l));
        }
    }
    const std::filesystem::path tempPath =
        path.parent_path() / (path.filename().string() + ".tmp.png");
    std::filesystem::remove(tempPath, ec);
    ec.clear();
    if (stbi_write_png(tempPath.string().c_str(), width, height, 4, displayPixels.data(), width * 4) == 0) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::filesystem::remove(path, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

bool ValidateDevelopRealRawSmoke(int rawArgCount, char** rawArgs) {
    std::filesystem::path previewDirectory;
    bool writeStagePreviews = false;
    bool writeControlPreviews = false;
    bool writeColorPreviews = false;
    std::vector<const char*> rawPaths;
    rawPaths.reserve(static_cast<std::size_t>((std::max)(0, rawArgCount)));
    for (int i = 0; i < rawArgCount; ++i) {
        if (std::strcmp(rawArgs[i], "--write-stage-previews") == 0) {
            writeStagePreviews = true;
            continue;
        }
        if (std::strcmp(rawArgs[i], "--write-control-previews") == 0) {
            writeControlPreviews = true;
            continue;
        }
        if (std::strcmp(rawArgs[i], "--write-color-previews") == 0) {
            writeColorPreviews = true;
            continue;
        }
        if (std::strcmp(rawArgs[i], "--write-previews") == 0) {
            if (i + 1 >= rawArgCount) {
                std::cerr << "Develop real RAW smoke validation failed: --write-previews needs a folder path.\n";
                return false;
            }
            previewDirectory = rawArgs[++i];
            continue;
        }
        rawPaths.push_back(rawArgs[i]);
    }

    if (rawPaths.empty()) {
        std::cerr << "Develop real RAW smoke validation failed: pass at least one RAW path.\n";
        return false;
    }

    if (writeStagePreviews && previewDirectory.empty()) {
        std::cerr << "Develop real RAW smoke validation failed: --write-stage-previews requires --write-previews <folder>.\n";
        return false;
    }
    if (writeControlPreviews && previewDirectory.empty()) {
        std::cerr << "Develop real RAW smoke validation failed: --write-control-previews requires --write-previews <folder>.\n";
        return false;
    }
    if (writeColorPreviews && previewDirectory.empty()) {
        std::cerr << "Develop real RAW smoke validation failed: --write-color-previews requires --write-previews <folder>.\n";
        return false;
    }

    if (!glfwInit()) {
        std::cerr << "Develop real RAW smoke validation failed: glfwInit() failed.\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Develop Real RAW Validation", nullptr, nullptr);
    if (!window) {
        std::cerr << "Develop real RAW smoke validation failed: unable to create hidden OpenGL window.\n";
        glfwTerminate();
        return false;
    }

    bool success = true;
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Develop real RAW smoke validation failed: unable to load OpenGL functions.\n";
        success = false;
    } else {
        RenderPipeline graphPipeline;
        graphPipeline.Initialize();
        graphPipeline.SetPreviewMaxDimension(1024);
        graphPipeline.Resize(64, 64);

        auto runDevelopGraph = [&](const Raw::RawImageData& raw,
                                   const EditorNodeGraph::RawDevelopPayload& payload,
                                   std::uint64_t requestRevision,
                                   float& outMaxRgb,
                                   std::vector<ToneCurveAutoRewriteFeedback>& outFeedbacks,
                                   std::vector<unsigned char>* outPixels = nullptr,
                                   int* outPixelW = nullptr,
                                   int* outPixelH = nullptr) {
            RenderGraphSnapshot graph;
            graph.outputNodeId = 3;

            RenderGraphNode rawSourceNode;
            rawSourceNode.nodeId = 1;
            rawSourceNode.kind = RenderGraphNodeKind::RawSource;
            rawSourceNode.requestRevision = requestRevision;
            rawSourceNode.rawSource.metadata = raw.metadata;
            rawSourceNode.rawSource.embeddedRawData = raw;
            graph.nodes.push_back(std::move(rawSourceNode));

            RenderGraphNode developNode;
            developNode.nodeId = 2;
            developNode.kind = RenderGraphNodeKind::RawDevelop;
            developNode.requestRevision = requestRevision;
            developNode.rawDevelop.settings = payload.settings;
            developNode.rawDevelop.scenePrepEnabled = payload.scenePrepEnabled;
            developNode.rawDevelop.scenePrepSettings = payload.scenePrepSettings;
            developNode.rawDevelop.integratedToneEnabled = payload.integratedToneEnabled;
            developNode.rawDevelop.integratedToneLayerJson = payload.integratedToneLayerJson;
            graph.nodes.push_back(std::move(developNode));

            RenderGraphNode outputNode;
            outputNode.nodeId = 3;
            outputNode.kind = RenderGraphNodeKind::Output;
            outputNode.requestRevision = requestRevision;
            graph.nodes.push_back(std::move(outputNode));

            graph.links.push_back(RenderGraphLink{ 1, "rawOut", 2, "rawIn" });
            graph.links.push_back(RenderGraphLink{ 2, "imageOut", 3, "imageIn" });

            graphPipeline.Resize(64, 64);
            graphPipeline.ExecuteGraph(graph);
            const int outputW = graphPipeline.GetCanvasWidth();
            const int outputH = graphPipeline.GetCanvasHeight();
            outMaxRgb = ReadTextureMaxRgb(graphPipeline.GetOutputTexture(), outputW, outputH);
            int pixelW = 0;
            int pixelH = 0;
            const std::vector<unsigned char> outputPixels = graphPipeline.GetOutputPixels(pixelW, pixelH);
            if (outPixels) {
                *outPixels = outputPixels;
            }
            if (outPixelW) {
                *outPixelW = pixelW;
            }
            if (outPixelH) {
                *outPixelH = pixelH;
            }
            outFeedbacks = graphPipeline.GetToneCurveAutoRewriteFeedback();
            return graphPipeline.GetOutputTexture() != 0 &&
                outputW > 0 &&
                outputH > 0 &&
                pixelW == outputW &&
                pixelH == outputH &&
                !outputPixels.empty() &&
                outMaxRgb > 0.01f;
        };

        for (std::size_t i = 0; i < rawPaths.size(); ++i) {
            const std::filesystem::path path = ResolveValidationInputPath(rawPaths[i]);
            const std::uint64_t requestBase = 20 + static_cast<std::uint64_t>(i) * 32;
            Raw::RawImageData raw;
            if (!Raw::RawLoader::LoadFile(path.string(), raw)) {
                std::cerr << "Develop real RAW smoke validation failed: unable to load "
                          << path.string() << " (" << raw.metadata.error << ")\n";
                success = false;
                continue;
            }

            EditorNodeGraph::RawDevelopPayload payload = BuildDevelopSmokeAutoPayload(
                0.04f, 0.16f, 0.86f, 0.002f, 0.18f, 0.24f, 3.20f, 0, 0.12f);
            payload.integratedToneLayerJson["autoCalibratePending"] = true;
            payload.integratedToneLayerJson["autoCalibrateRequestId"] = requestBase - 10;
            EditorModule::ApplyDevelopAutoSolve(payload, raw.metadata, true);

            float firstMaxRgb = 0.0f;
            std::vector<ToneCurveAutoRewriteFeedback> firstFeedbacks;
            const bool firstRenderOk = runDevelopGraph(raw, payload, requestBase, firstMaxRgb, firstFeedbacks);
            if (!firstFeedbacks.empty() && firstFeedbacks.front().valid) {
                payload.integratedToneLayerJson = firstFeedbacks.front().authoredLayerJson;
                EditorModule::ApplyDevelopAutoSolve(payload, raw.metadata, true);
            }

            const Raw::RawDevelopSettings settingsAfterSolve = payload.settings;
            const Raw::RawDetailFusionSettings prepAfterSolve = payload.scenePrepSettings;
            EditorModule::ApplyDevelopAutoSolve(payload, raw.metadata, true);
            const bool repeatedSolveStable =
                std::abs(payload.settings.exposureStops - settingsAfterSolve.exposureStops) < 0.0001f &&
                payload.settings.highlightMode == settingsAfterSolve.highlightMode &&
                std::abs(payload.settings.highlightStrength - settingsAfterSolve.highlightStrength) < 0.0001f &&
                std::abs(payload.scenePrepSettings.strength - prepAfterSolve.strength) < 0.0001f &&
                std::abs(payload.scenePrepSettings.highlightProtectionBias - prepAfterSolve.highlightProtectionBias) < 0.0001f;

            float finalMaxRgb = 0.0f;
            std::vector<ToneCurveAutoRewriteFeedback> finalFeedbacks;
            std::vector<unsigned char> finalPixels;
            int finalPixelW = 0;
            int finalPixelH = 0;
            const bool finalRenderOk = runDevelopGraph(
                raw,
                payload,
                requestBase + 1,
                finalMaxRgb,
                finalFeedbacks,
                &finalPixels,
                &finalPixelW,
                &finalPixelH);

            bool previewWritten = true;
            std::filesystem::path previewPath;
            const std::string previewStem = SanitizeValidationFileStem(path.stem().string());
            if (!previewDirectory.empty()) {
                previewPath = previewDirectory /
                    (previewStem + "_develop_auto.png");
                previewWritten = finalRenderOk &&
                    WriteValidationPng(previewPath, finalPixels, finalPixelW, finalPixelH);
                if (!previewWritten) {
                    std::cerr
                        << "Develop real RAW smoke validation failed: unable to write preview "
                        << previewPath.string() << "\n";
                }
            }

            const ValidationColorStats finalColorStats = ComputeValidationColorStats(finalPixels);
            const ValidationFineNoiseStats finalFineNoiseStats =
                ComputeValidationFineNoiseStats(finalPixels, finalPixelW, finalPixelH);

            bool stagePreviewsOk = true;
            if (writeStagePreviews) {
                struct StagePreviewSpec {
                    const char* label = "";
                    const char* suffix = "";
                    bool scenePrepEnabled = false;
                    bool integratedToneEnabled = false;
                };
                const std::array<StagePreviewSpec, 3> stageSpecs { {
                    { "raw_exposure", "_raw_exposure.png", false, false },
                    { "raw_scene_prep", "_raw_scene_prep.png", true, false },
                    { "raw_tone", "_raw_tone.png", false, true },
                } };

                for (std::size_t stageIndex = 0; stageIndex < stageSpecs.size(); ++stageIndex) {
                    const StagePreviewSpec& spec = stageSpecs[stageIndex];
                    EditorNodeGraph::RawDevelopPayload stagePayload = payload;
                    stagePayload.scenePrepEnabled = spec.scenePrepEnabled;
                    stagePayload.integratedToneEnabled = spec.integratedToneEnabled;

                    float stageMaxRgb = 0.0f;
                    std::vector<ToneCurveAutoRewriteFeedback> stageFeedbacks;
                    std::vector<unsigned char> stagePixels;
                    int stagePixelW = 0;
                    int stagePixelH = 0;
                    const bool stageRenderOk = runDevelopGraph(
                        raw,
                        stagePayload,
                        requestBase + 2 + static_cast<std::uint64_t>(stageIndex),
                        stageMaxRgb,
                        stageFeedbacks,
                        &stagePixels,
                        &stagePixelW,
                        &stagePixelH);
                    const std::filesystem::path stagePath = previewDirectory / (previewStem + spec.suffix);
                    const bool stageWriteOk = stageRenderOk &&
                        WriteValidationPng(stagePath, stagePixels, stagePixelW, stagePixelH);
                    stagePreviewsOk = stagePreviewsOk && stageWriteOk;
                    std::cout
                        << "Develop real RAW stage preview: " << path.filename().string()
                        << " stage=" << spec.label
                        << " maxRgb=" << stageMaxRgb
                        << " avgLuma=" << ComputeAverageNormalizedLuma(stagePixels)
                        << " renderOk=" << stageRenderOk
                        << " previewWritten=" << stageWriteOk;
                    if (stageWriteOk) {
                        std::cout << " preview=" << stagePath.string();
                    }
                    std::cout << "\n";
                    if (!stageWriteOk) {
                        std::cerr
                            << "Develop real RAW smoke validation failed: unable to write stage preview "
                            << stagePath.string()
                            << " renderOk=" << stageRenderOk
                            << "\n";
                    }
                }
                previewWritten = previewWritten && stagePreviewsOk;
            }

            bool controlPreviewsOk = true;
            if (writeControlPreviews) {
                struct ControlPreviewSpec {
                    const char* label = "";
                    const char* suffix = "";
                    float rawExposureDelta = 0.0f;
                    float scenePrepAmountDelta = 0.0f;
                    int rotationDegrees = -1;
                    int mosaicDenoiseVariant = 0;
                };

                const std::array<ControlPreviewSpec, 9> controlSpecs { {
                    { "manual_raw_exposure_plus_0_75", "_manual_raw_exposure_plus_0_75.png", 0.75f, 0.0f, -1, 0 },
                    { "manual_raw_exposure_minus_0_75", "_manual_raw_exposure_minus_0_75.png", -0.75f, 0.0f, -1, 0 },
                    { "manual_scene_prep_amount_plus_0_25", "_manual_scene_prep_amount_plus_0_25.png", 0.0f, 0.25f, -1, 0 },
                    { "manual_scene_prep_amount_minus_0_25", "_manual_scene_prep_amount_minus_0_25.png", 0.0f, -0.25f, -1, 0 },
                    { "manual_orientation_rotate_90", "_manual_orientation_rotate_90.png", 0.0f, 0.0f, 90, 0 },
                    { "manual_orientation_rotate_180", "_manual_orientation_rotate_180.png", 0.0f, 0.0f, 180, 0 },
                    { "manual_orientation_rotate_270", "_manual_orientation_rotate_270.png", 0.0f, 0.0f, 270, 0 },
                    { "manual_mosaic_denoise_off", "_manual_mosaic_denoise_off.png", 0.0f, 0.0f, -1, 1 },
                    { "manual_mosaic_denoise_stronger", "_manual_mosaic_denoise_stronger.png", 0.0f, 0.0f, -1, 2 },
                } };

                EditorNodeGraph::RawDevelopPayload controlBasePayload = payload;
                if (!finalFeedbacks.empty() && finalFeedbacks.front().valid) {
                    controlBasePayload.integratedToneLayerJson = finalFeedbacks.front().authoredLayerJson;
                }
                if (controlBasePayload.integratedToneLayerJson.is_object()) {
                    controlBasePayload.integratedToneLayerJson["autoCalibratePending"] = false;
                }

                for (std::size_t controlIndex = 0; controlIndex < controlSpecs.size(); ++controlIndex) {
                    const ControlPreviewSpec& spec = controlSpecs[controlIndex];
                    EditorNodeGraph::RawDevelopPayload controlPayload = controlBasePayload;
                    controlPayload.settings.exposureStops = std::clamp(
                        controlPayload.settings.exposureStops + spec.rawExposureDelta,
                        -8.0f,
                        8.0f);
                    controlPayload.scenePrepSettings.strength = std::clamp(
                        controlPayload.scenePrepSettings.strength + spec.scenePrepAmountDelta,
                        0.0f,
                        1.25f);
                    if (spec.rotationDegrees >= 0) {
                        controlPayload.settings.rotationDegrees = spec.rotationDegrees;
                    }
                    if (spec.mosaicDenoiseVariant == 1) {
                        controlPayload.settings.mosaicDenoise.enabled = false;
                    } else if (spec.mosaicDenoiseVariant == 2) {
                        controlPayload.settings.mosaicDenoise.enabled = true;
                        controlPayload.settings.mosaicDenoise.hotPixelSuppression = true;
                        controlPayload.settings.mosaicDenoise.hotPixelThreshold = std::min(
                            controlPayload.settings.mosaicDenoise.hotPixelThreshold,
                            0.07f);
                        controlPayload.settings.mosaicDenoise.lumaStrength = std::clamp(
                            controlPayload.settings.mosaicDenoise.lumaStrength + 0.12f,
                            0.0f,
                            1.0f);
                        controlPayload.settings.mosaicDenoise.chromaStrength = std::clamp(
                            controlPayload.settings.mosaicDenoise.chromaStrength + 0.08f,
                            0.0f,
                            1.0f);
                        controlPayload.settings.mosaicDenoise.radius = 4;
                        controlPayload.settings.mosaicDenoise.iterations = 2;
                        controlPayload.settings.mosaicDenoise.edgeProtection = std::clamp(
                            controlPayload.settings.mosaicDenoise.edgeProtection - 0.05f,
                            0.0f,
                            1.0f);
                    }

                    float controlMaxRgb = 0.0f;
                    std::vector<ToneCurveAutoRewriteFeedback> controlFeedbacks;
                    std::vector<unsigned char> controlPixels;
                    int controlPixelW = 0;
                    int controlPixelH = 0;
                    const bool controlRenderOk = runDevelopGraph(
                        raw,
                        controlPayload,
                        requestBase + 6 + static_cast<std::uint64_t>(controlIndex),
                        controlMaxRgb,
                        controlFeedbacks,
                        &controlPixels,
                        &controlPixelW,
                        &controlPixelH);
                    const std::filesystem::path controlPath = previewDirectory / (previewStem + spec.suffix);
                    const bool controlWriteOk = controlRenderOk &&
                        WriteValidationPng(controlPath, controlPixels, controlPixelW, controlPixelH);
                    const ValidationColorStats controlColorStats = ComputeValidationColorStats(controlPixels);
                    const ValidationFineNoiseStats controlFineNoiseStats =
                        ComputeValidationFineNoiseStats(controlPixels, controlPixelW, controlPixelH);
                    controlPreviewsOk = controlPreviewsOk && controlWriteOk;
                    std::cout
                        << "Develop real RAW control preview: " << path.filename().string()
                        << " control=" << spec.label
                        << " exposure=" << controlPayload.settings.exposureStops
                        << " exposureDelta=" << spec.rawExposureDelta
                        << " scenePrepAmount=" << controlPayload.scenePrepSettings.strength
                        << " scenePrepAmountDelta=" << spec.scenePrepAmountDelta
                        << " rotationDegrees=" << controlPayload.settings.rotationDegrees
                        << " mosaicDenoise="
                        << controlPayload.settings.mosaicDenoise.enabled
                        << "," << controlPayload.settings.mosaicDenoise.lumaStrength
                        << "," << controlPayload.settings.mosaicDenoise.chromaStrength
                        << "," << controlPayload.settings.mosaicDenoise.radius
                        << "," << controlPayload.settings.mosaicDenoise.iterations
                        << "," << controlPayload.settings.mosaicDenoise.edgeProtection
                        << " output=" << controlPixelW << "x" << controlPixelH
                        << " maxRgb=" << controlMaxRgb
                        << " maxRgbDelta=" << (controlMaxRgb - finalMaxRgb)
                        << " avgLuma=" << controlColorStats.avgLuma
                        << " avgLumaDelta=" << (controlColorStats.avgLuma - finalColorStats.avgLuma)
                        << " colorBiasRisk=" << controlColorStats.biasRisk
                        << " fineNoise=" << controlFineNoiseStats.combined
                        << " fineNoiseDelta=" << (controlFineNoiseStats.combined - finalFineNoiseStats.combined)
                        << " renderOk=" << controlRenderOk
                        << " previewWritten=" << controlWriteOk;
                    if (controlWriteOk) {
                        std::cout << " preview=" << controlPath.string();
                    }
                    std::cout << "\n";
                    if (!controlWriteOk) {
                        std::cerr
                            << "Develop real RAW smoke validation failed: unable to write control preview "
                            << controlPath.string()
                            << " renderOk=" << controlRenderOk
                            << "\n";
                    }
                }
                previewWritten = previewWritten && controlPreviewsOk;
            }

            bool colorPreviewsOk = true;
            if (writeColorPreviews) {
                struct ColorPreviewSpec {
                    const char* label = "";
                    const char* suffix = "";
                    bool overrideWhiteBalance = false;
                    Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
                    bool overrideCameraTransform = false;
                    Raw::RawCameraTransformSource cameraTransformSource = Raw::RawCameraTransformSource::DngAuto;
                    bool cameraTransformEnabled = true;
                    bool dngOnly = false;
                };

                const std::array<ColorPreviewSpec, 8> colorSpecs { {
                    { "white_balance_auto", "_color_wb_auto.png", true, Raw::WhiteBalanceMode::Auto, false, Raw::RawCameraTransformSource::DngAuto, true, false },
                    { "white_balance_neutral", "_color_wb_neutral.png", true, Raw::WhiteBalanceMode::Neutral, false, Raw::RawCameraTransformSource::DngAuto, true, false },
                    { "camera_transform_off", "_color_camera_transform_off.png", false, Raw::WhiteBalanceMode::AsShot, true, Raw::RawCameraTransformSource::DngAuto, false, false },
                    { "camera_libraw_rgb_cam", "_color_camera_libraw_rgb_cam.png", false, Raw::WhiteBalanceMode::AsShot, true, Raw::RawCameraTransformSource::LibRawRgbCam, true, false },
                    { "dng_auto", "_color_dng_auto.png", false, Raw::WhiteBalanceMode::AsShot, true, Raw::RawCameraTransformSource::DngAuto, true, true },
                    { "dng_forward_matrix_1", "_color_dng_forward_matrix_1.png", false, Raw::WhiteBalanceMode::AsShot, true, Raw::RawCameraTransformSource::DngForwardMatrix1, true, true },
                    { "dng_forward_matrix_2", "_color_dng_forward_matrix_2.png", false, Raw::WhiteBalanceMode::AsShot, true, Raw::RawCameraTransformSource::DngForwardMatrix2, true, true },
                    { "dng_color_matrix_inverse", "_color_dng_color_matrix_inverse.png", false, Raw::WhiteBalanceMode::AsShot, true, Raw::RawCameraTransformSource::DngColorMatrixInverse, true, true },
                } };

                EditorNodeGraph::RawDevelopPayload colorBasePayload = payload;
                if (!finalFeedbacks.empty() && finalFeedbacks.front().valid) {
                    colorBasePayload.integratedToneLayerJson = finalFeedbacks.front().authoredLayerJson;
                }
                if (colorBasePayload.integratedToneLayerJson.is_object()) {
                    colorBasePayload.integratedToneLayerJson["autoCalibratePending"] = false;
                }

                for (std::size_t colorIndex = 0; colorIndex < colorSpecs.size(); ++colorIndex) {
                    const ColorPreviewSpec& spec = colorSpecs[colorIndex];
                    if (spec.dngOnly && !raw.metadata.isDng) {
                        continue;
                    }
                    if (spec.cameraTransformSource == Raw::RawCameraTransformSource::DngForwardMatrix1 &&
                        !raw.metadata.hasDngForwardMatrix1) {
                        continue;
                    }
                    if (spec.cameraTransformSource == Raw::RawCameraTransformSource::DngForwardMatrix2 &&
                        !raw.metadata.hasDngForwardMatrix2) {
                        continue;
                    }
                    if (spec.cameraTransformSource == Raw::RawCameraTransformSource::DngColorMatrixInverse &&
                        !raw.metadata.hasDngColorMatrix1 &&
                        !raw.metadata.hasDngColorMatrix2) {
                        continue;
                    }

                    EditorNodeGraph::RawDevelopPayload colorPayload = colorBasePayload;
                    if (spec.overrideWhiteBalance) {
                        colorPayload.settings.whiteBalanceMode = spec.whiteBalanceMode;
                    }
                    if (spec.overrideCameraTransform) {
                        colorPayload.settings.cameraTransformSource = spec.cameraTransformSource;
                        colorPayload.settings.cameraTransformEnabled = spec.cameraTransformEnabled;
                    }

                    float colorMaxRgb = 0.0f;
                    std::vector<ToneCurveAutoRewriteFeedback> colorFeedbacks;
                    std::vector<unsigned char> colorPixels;
                    int colorPixelW = 0;
                    int colorPixelH = 0;
                    const bool colorRenderOk = runDevelopGraph(
                        raw,
                        colorPayload,
                        requestBase + 10 + static_cast<std::uint64_t>(colorIndex),
                        colorMaxRgb,
                        colorFeedbacks,
                        &colorPixels,
                        &colorPixelW,
                        &colorPixelH);
                    const std::filesystem::path colorPath = previewDirectory / (previewStem + spec.suffix);
                    const bool colorWriteOk = colorRenderOk &&
                        WriteValidationPng(colorPath, colorPixels, colorPixelW, colorPixelH);
                    const ValidationColorStats colorStats = ComputeValidationColorStats(colorPixels);
                    colorPreviewsOk = colorPreviewsOk && colorWriteOk;
                    std::cout
                        << "Develop real RAW color preview: " << path.filename().string()
                        << " color=" << spec.label
                        << " wbMode=" << Raw::WhiteBalanceModeName(colorPayload.settings.whiteBalanceMode)
                        << " cameraTransform=" << Raw::RawCameraTransformSourceName(colorPayload.settings.cameraTransformSource)
                        << " cameraTransformEnabled=" << colorPayload.settings.cameraTransformEnabled
                        << " maxRgb=" << colorMaxRgb
                        << " avgLuma=" << colorStats.avgLuma
                        << " avgRgb=" << colorStats.avgR
                        << "," << colorStats.avgG
                        << "," << colorStats.avgB
                        << " channelRatio=" << colorStats.channelRatio
                        << " warmCoolBias=" << colorStats.warmCoolBias
                        << " magentaGreenBias=" << colorStats.magentaGreenBias
                        << " colorBiasRisk=" << colorStats.biasRisk
                        << " renderOk=" << colorRenderOk
                        << " previewWritten=" << colorWriteOk;
                    if (colorWriteOk) {
                        std::cout << " preview=" << colorPath.string();
                    }
                    std::cout << "\n";
                    if (!colorWriteOk) {
                        std::cerr
                            << "Develop real RAW smoke validation failed: unable to write color preview "
                            << colorPath.string()
                            << " renderOk=" << colorRenderOk
                            << "\n";
                    }
                }
                previewWritten = previewWritten && colorPreviewsOk;
            }

            const bool rawOk = firstRenderOk && finalRenderOk && repeatedSolveStable && previewWritten;
            const std::array<float, 3> resolvedWhiteBalance =
                ComputeValidationResolvedWhiteBalance(raw.metadata, payload.settings);
            const float dngAutoBlend = ComputeValidationDngAutoBlend(raw.metadata);
            std::cout
                << "Develop real RAW smoke: " << path.filename().string()
                << " orientation=" << raw.metadata.orientation
                << " display=" << Raw::DisplayWidth(raw.metadata) << "x" << Raw::DisplayHeight(raw.metadata)
                << " layout=" << Raw::RawPixelLayoutName(raw.metadata.pixelLayout)
                << " cfa=" << Raw::CfaPatternName(raw.metadata.cfaPattern)
                << " wbMode=" << Raw::WhiteBalanceModeName(payload.settings.whiteBalanceMode)
                << " wbSource=\"" << raw.metadata.whiteBalanceSource << "\""
                << " wbResolved=" << resolvedWhiteBalance[0]
                << "," << resolvedWhiteBalance[1]
                << "," << resolvedWhiteBalance[2]
                << " camWb=" << raw.metadata.cameraWhiteBalance[0]
                << "," << raw.metadata.cameraWhiteBalance[1]
                << "," << raw.metadata.cameraWhiteBalance[2]
                << " dayWb=" << raw.metadata.daylightWhiteBalance[0]
                << "," << raw.metadata.daylightWhiteBalance[1]
                << "," << raw.metadata.daylightWhiteBalance[2]
                << " asShotNeutral=" << raw.metadata.dngAsShotNeutral[0]
                << "," << raw.metadata.dngAsShotNeutral[1]
                << "," << raw.metadata.dngAsShotNeutral[2]
                << " analogBalance=" << raw.metadata.dngAnalogBalance[0]
                << "," << raw.metadata.dngAnalogBalance[1]
                << "," << raw.metadata.dngAnalogBalance[2]
                << " cameraTransform=" << Raw::RawCameraTransformSourceName(payload.settings.cameraTransformSource)
                << " matrixSource=\"" << raw.metadata.cameraMatrixSource << "\""
                << " dngAutoBlend=" << dngAutoBlend
                << " dngMatrices=C1:" << raw.metadata.hasDngColorMatrix1
                << ",C2:" << raw.metadata.hasDngColorMatrix2
                << ",F1:" << raw.metadata.hasDngForwardMatrix1
                << ",F2:" << raw.metadata.hasDngForwardMatrix2
                << ",CC1:" << raw.metadata.hasDngCameraCalibration1
                << ",CC2:" << raw.metadata.hasDngCameraCalibration2
                << ",AB:" << raw.metadata.hasDngAnalogBalance
                << " illuminants=" << raw.metadata.dngIlluminant1
                << "," << raw.metadata.dngIlluminant2
                << " dngBaselineExposure=" << (raw.metadata.hasDngBaselineExposure ? raw.metadata.dngBaselineExposure : 0.0f)
                << " black=" << raw.metadata.blackLevel
                << " white=" << raw.metadata.whiteLevel
                << " gainMaps=" << raw.metadata.dngGainMapCount
                << " unsupportedOpcodes=" << raw.metadata.dngUnsupportedOpcodeCount
                << " metadataWarnings=" << raw.metadata.warnings.size()
                << " mosaicDenoise=" << payload.settings.mosaicDenoise.enabled
                << "," << payload.settings.mosaicDenoise.lumaStrength
                << "," << payload.settings.mosaicDenoise.chromaStrength
                << "," << payload.settings.mosaicDenoise.radius
                << "," << payload.settings.mosaicDenoise.iterations
                << "," << payload.settings.mosaicDenoise.edgeProtection
                << "," << payload.settings.mosaicDenoise.hotPixelThreshold
                << "," << payload.settings.mosaicDenoise.hotPixelSuppression
                << " firstMaxRgb=" << firstMaxRgb
                << " finalMaxRgb=" << finalMaxRgb
                << " finalAvgLuma=" << finalColorStats.avgLuma
                << " finalAvgRgb=" << finalColorStats.avgR
                << "," << finalColorStats.avgG
                << "," << finalColorStats.avgB
                << " finalChroma=" << finalColorStats.avgPixelChroma
                << " finalChannelRatio=" << finalColorStats.channelRatio
                << " finalWarmCoolBias=" << finalColorStats.warmCoolBias
                << " finalMagentaGreenBias=" << finalColorStats.magentaGreenBias
                << " finalColorBiasRisk=" << finalColorStats.biasRisk
                << " finalFineNoise=" << finalFineNoiseStats.combined
                << "," << finalFineNoiseStats.lumaHighFrequency
                << "," << finalFineNoiseStats.chromaHighFrequency
                << " exposure=" << payload.settings.exposureStops
                << " scenePrepStrength=" << payload.scenePrepSettings.strength
                << " scenePrepMaxEvBias=" << payload.scenePrepSettings.maxEvBias
                << " scenePrepTarget=" << payload.scenePrepSettings.wellExposedTarget
                << " scenePrepTargetBias=" << payload.scenePrepSettings.wellExposedTargetBias
                << " highlightBias=" << payload.scenePrepSettings.highlightProtectionBias
                << " statShadow=" << payload.integratedToneLayerJson.value("autoSceneShadowPercentile", -1.0f)
                << " statMid=" << payload.integratedToneLayerJson.value("autoSceneMidtonePercentile", -1.0f)
                << " statHighlight=" << payload.integratedToneLayerJson.value("autoSceneHighlightPercentile", -1.0f)
                << " statNoise=" << payload.integratedToneLayerJson.value("autoSceneNoiseRisk", -1.0f)
                << " statPressure=" << payload.integratedToneLayerJson.value("autoSceneHighlightPressure", -1.0f)
                << " statHdrEv=" << payload.integratedToneLayerJson.value("autoSceneHdrSpreadEv", -1.0f)
                << " statProfile=" << payload.integratedToneLayerJson.value("autoSceneProfile", -1)
                << " toneMiddleGrey=" << payload.integratedToneLayerJson.value("middleGrey", -1.0f)
                << " toneLocalStrength=" << payload.integratedToneLayerJson.value("localBaselineStrength", -1.0f)
                << " toneShadowOpening=" << payload.integratedToneLayerJson.value("localShadowOpening", -1.0f)
                << " toneHighlightCompression=" << payload.integratedToneLayerJson.value("localHighlightCompression", -1.0f)
                << " toneFoundation=" << payload.integratedToneLayerJson.value("foundationShadows", -9.0f)
                << "," << payload.integratedToneLayerJson.value("foundationDarks", -9.0f)
                << "," << payload.integratedToneLayerJson.value("foundationMidtones", -9.0f)
                << "," << payload.integratedToneLayerJson.value("foundationLights", -9.0f)
                << "," << payload.integratedToneLayerJson.value("foundationHighlights", -9.0f)
                << " renderToneNoise=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().noiseRisk : -1.0f)
                << " renderTonePressure=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().highlightPressure : -1.0f)
                << " renderToneHdrEv=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().hdrSpreadEv : -1.0f)
                << " renderToneProfile=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().sceneProfile : -1)
                << " renderToneMiddleGrey=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("middleGrey", -1.0f) : -1.0f)
                << " renderToneLocalStrength=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("localBaselineStrength", -1.0f) : -1.0f)
                << " renderToneShadowOpening=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("localShadowOpening", -1.0f) : -1.0f)
                << " renderToneHighlightCompression=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("localHighlightCompression", -1.0f) : -1.0f)
                << " renderToneFoundation=" << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("foundationShadows", -9.0f) : -9.0f)
                << "," << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("foundationDarks", -9.0f) : -9.0f)
                << "," << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("foundationMidtones", -9.0f) : -9.0f)
                << "," << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("foundationLights", -9.0f) : -9.0f)
                << "," << (!finalFeedbacks.empty() ? finalFeedbacks.front().authoredLayerJson.value("foundationHighlights", -9.0f) : -9.0f)
                << " repeatedSolveStable=" << repeatedSolveStable;
            if (!previewPath.empty() && previewWritten) {
                std::cout << " preview=" << previewPath.string();
            }
            std::cout
                << "\n";
            if (!rawOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed for " << path.string()
                    << ": firstRenderOk=" << firstRenderOk
                    << " finalRenderOk=" << finalRenderOk
                    << " repeatedSolveStable=" << repeatedSolveStable
                    << " previewWritten=" << previewWritten
                    << " firstFeedbacks=" << firstFeedbacks.size()
                    << " finalFeedbacks=" << finalFeedbacks.size()
                    << "\n";
                success = false;
            }
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    if (success) {
        std::cout << "Develop real RAW smoke validation passed." << std::endl;
    }
    return success;
}

} // namespace

int main(int argc, char** argv) {
    SetWorkingDirectoryToExecutableFolder();

    if (argc > 1 && std::strcmp(argv[1], "--validate-layer-registry") == 0) {
        std::vector<std::string> errors;
        if (!LayerRegistry::ValidateRegistry(&errors)) {
            for (const std::string& error : errors) {
                std::cerr << "LayerRegistry validation failed: " << error << std::endl;
            }
            return 2;
        }

        std::cout << "LayerRegistry validation passed." << std::endl;
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-tone-curve-auto") == 0) {
        return ValidateToneCurveAutoIntegration() ? 0 : 4;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-develop-auto-solve") == 0) {
        return ValidateDevelopAutoSolveBehavior() ? 0 : 7;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-develop-node-smoke") == 0) {
        return ValidateDevelopNodeSmoke() ? 0 : 5;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-develop-real-raw-smoke") == 0) {
        return ValidateDevelopRealRawSmoke(argc - 2, argv + 2) ? 0 : 6;
    }

    AppShell app;
    
    std::cout << "Starting Modular Studio: Stack..." << std::endl;

    if (!app.Initialize("Stack", 1280, 800)) {
        std::cerr << "Failed to initialize Application Shell!" << std::endl;
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}
