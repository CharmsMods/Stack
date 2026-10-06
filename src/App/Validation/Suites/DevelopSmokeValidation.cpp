#include "App/Validation/ValidationSuites.h"

#include "App/Validation/ValidationImageUtils.h"
#include "Editor/EditorModule.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Raw/RawLoader.h"
#include "Raw/RawGpuPipeline.h"
#include "Raw/RawGpuPreprocessor.h"
#include "Raw/RawProcessingMath.h"
#include "Renderer/GLLoader.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <GLFW/glfw3.h>

namespace {

using Stack::Validation::ComputeAverageNormalizedLuma;
using Stack::Validation::ComputeValidationColorStats;
using Stack::Validation::ComputeValidationFineNoiseStats;
using Stack::Validation::CountPixelsWithNonZeroAlpha;
using Stack::Validation::CountPixelsWithNonZeroRgb;
using Stack::Validation::ReadTextureMaxRgb;
using Stack::Validation::ReadTextureRgbaFloat;
using Stack::Validation::ResolveValidationInputPath;
using Stack::Validation::SanitizeValidationFileStem;
using Stack::Validation::ValidationColorStats;
using Stack::Validation::ValidationFineNoiseStats;
using Stack::Validation::WriteValidationPng;

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

bool ValidateRawGpuPreprocessParity(
    float& outNormalizedMaxError,
    float& outVarianceMaxError) {
    constexpr int width = 10;
    constexpr int height = 8;
    Raw::RawImageData raw;
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = 7;
    raw.metadata.visibleHeight = 6;
    raw.metadata.leftMargin = 2;
    raw.metadata.topMargin = 1;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.hasDngActiveArea = true;
    raw.metadata.dngActiveArea = { 1, 2, 7, 9 };
    raw.metadata.dngCfaRepeatPatternDim = { 2, 2 };
    raw.metadata.dngCfaPattern = { 0, 1, 1, 2 };
    raw.metadata.dngCfaPlaneColor = { 0, 1, 2 };
    raw.metadata.blackLevel = 64.0f;
    raw.metadata.perChannelBlack = { 72.0f, 68.0f, 80.0f, 0.0f };
    raw.metadata.whiteLevel = 3800.0f;
    raw.metadata.dngBlackLevelRepeatDim = { 2, 2 };
    raw.metadata.dngBlackLevelValues = { 64.0f, 67.0f, 70.0f, 73.0f };
    raw.metadata.dngBlackLevelDeltaH = {
        0.0f, 0.5f, -0.25f, 0.75f, -0.5f, 0.25f, 0.0f
    };
    raw.metadata.dngBlackLevelDeltaV = {
        0.0f, -0.4f, 0.2f, 0.6f, -0.3f, 0.1f
    };
    raw.metadata.dngWhiteLevelValues = { 3500.0f, 3600.0f, 3700.0f };
    raw.metadata.dngLinearizationTable.resize(4096);
    for (std::size_t index = 0;
         index < raw.metadata.dngLinearizationTable.size();
         ++index) {
        raw.metadata.dngLinearizationTable[index] =
            static_cast<std::uint16_t>(std::min<std::size_t>(
                4095u,
                index + index / 257u));
    }

    Raw::DngGainMapOpcode fullMap;
    fullMap.top = 0;
    fullMap.left = 0;
    fullMap.bottom = 6;
    fullMap.right = 7;
    fullMap.plane = 0;
    fullMap.planes = 1;
    fullMap.rowPitch = 1;
    fullMap.colPitch = 1;
    fullMap.mapPointsV = 3;
    fullMap.mapPointsH = 4;
    fullMap.mapPlanes = 2;
    fullMap.mapSpacingV = 0.42;
    fullMap.mapSpacingH = 0.31;
    fullMap.mapOriginV = 0.02;
    fullMap.mapOriginH = -0.03;
    fullMap.gains = {
        0.82f, 1.60f, 0.91f, 1.55f, 1.04f, 1.50f, 1.12f, 1.45f,
        0.88f, 1.40f, 0.97f, 1.35f, 1.09f, 1.30f, 1.18f, 1.25f,
        0.94f, 1.20f, 1.03f, 1.15f, 1.15f, 1.10f, 1.24f, 1.05f
    };
    Raw::DngGainMapOpcode pitchedMap;
    pitchedMap.top = 1;
    pitchedMap.left = 1;
    pitchedMap.bottom = 6;
    pitchedMap.right = 7;
    pitchedMap.plane = 0;
    pitchedMap.planes = 1;
    pitchedMap.rowPitch = 2;
    pitchedMap.colPitch = 2;
    pitchedMap.mapPointsV = 2;
    pitchedMap.mapPointsH = 2;
    pitchedMap.mapPlanes = 1;
    pitchedMap.mapSpacingV = 0.72;
    pitchedMap.mapSpacingH = 0.68;
    pitchedMap.mapOriginV = 0.08;
    pitchedMap.mapOriginH = 0.05;
    pitchedMap.gains = { 1.24f, 0.76f, 1.10f, 0.92f };
    raw.metadata.dngGainMaps = { fullMap, pitchedMap };
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = {
        Raw::DngNoiseProfilePlane { 0.0045, 0.000035 }
    };

    raw.rawBuffer.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            raw.rawBuffer[static_cast<std::size_t>(y) * width + x] =
                static_cast<std::uint16_t>(
                    180 + x * 271 + y * 193 + ((x + y) % 3) * 47);
        }
    }

    Raw::RawDevelopSettings settings;
    settings.mosaicDenoise.enabled = true;
    settings.mosaicDenoise.mode =
        Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    std::vector<float> expectedNormalized;
    std::vector<float> expectedVariance;
    std::string cpuError;
    if (!Raw::Processing::BuildTruthfulNormalizedMosaic(
            raw, settings, expectedNormalized, &cpuError) ||
        !Raw::Processing::BuildTruthfulNoiseVarianceMosaic(
            raw, settings, expectedVariance, &cpuError)) {
        std::cerr << "RAW GPU preprocess fixture setup failed: "
                  << cpuError << "\n";
        return false;
    }

    std::array<Raw::DngNoiseProfilePlane, 3> profiles {};
    if (!Raw::Processing::ResolveDngNoiseProfile(raw.metadata, profiles)) {
        return false;
    }

    unsigned int rawTexture = 0;
    unsigned int correctedTexture = 0;
    unsigned int varianceTexture = 0;
    glGenTextures(1, &rawTexture);
    glBindTexture(GL_TEXTURE_2D, rawTexture);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_R16UI, width, height, 0,
        GL_RED_INTEGER, GL_UNSIGNED_SHORT, raw.rawBuffer.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    Raw::RawGpuPreprocessor preprocessor;
    Raw::RawGpuPreprocessTelemetry telemetry;
    std::string gpuError;
    const bool dispatched = preprocessor.Process(
        raw,
        settings,
        rawTexture,
        0x12345678u,
        0x87654321u,
        true,
        profiles,
        correctedTexture,
        varianceTexture,
        telemetry,
        gpuError);
    glFinish();

    std::vector<float> gpuNormalized(expectedNormalized.size(), 0.0f);
    std::vector<float> gpuVariance(expectedVariance.size(), 0.0f);
    if (dispatched && correctedTexture != 0 && varianceTexture != 0) {
        glBindTexture(GL_TEXTURE_2D, correctedTexture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT,
            gpuNormalized.data());
        glBindTexture(GL_TEXTURE_2D, varianceTexture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT,
            gpuVariance.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    outNormalizedMaxError = 0.0f;
    outVarianceMaxError = 0.0f;
    for (std::size_t index = 0; index < expectedNormalized.size(); ++index) {
        outNormalizedMaxError = std::max(
            outNormalizedMaxError,
            std::abs(expectedNormalized[index] - gpuNormalized[index]));
        outVarianceMaxError = std::max(
            outVarianceMaxError,
            std::abs(expectedVariance[index] - gpuVariance[index]));
    }

    Raw::RawGpuPreprocessTelemetry cacheTelemetry;
    std::string cacheError;
    const bool cacheReuse = preprocessor.Process(
        raw,
        settings,
        rawTexture,
        0x12345678u,
        0x87654321u,
        true,
        profiles,
        correctedTexture,
        varianceTexture,
        cacheTelemetry,
        cacheError);
    const bool parity =
        dispatched &&
        telemetry.gpuDispatched &&
        !telemetry.cpuFallback &&
        telemetry.metadataUploadBytes > 0u &&
        outNormalizedMaxError <= 0.00002f &&
        outVarianceMaxError <= 0.000002f &&
        cacheReuse &&
        cacheTelemetry.correctedCacheHit &&
        !cacheTelemetry.gpuDispatched;
    if (!parity) {
        std::cerr
            << "RAW GPU preprocess parity failed: error=" << gpuError
            << " normalizedMaxError=" << outNormalizedMaxError
            << " varianceMaxError=" << outVarianceMaxError
            << " cacheError=" << cacheError
            << "\n";
    }

    preprocessor.Clear();
    if (varianceTexture != 0) glDeleteTextures(1, &varianceTexture);
    if (correctedTexture != 0) glDeleteTextures(1, &correctedTexture);
    if (rawTexture != 0) glDeleteTextures(1, &rawTexture);
    return parity;
}

Raw::RawImageData BuildSyntheticWhiteBalanceFlat(int width, int height) {
    Raw::RawImageData raw;
    raw.metadata = BuildSyntheticRawMetadata(width, height);
    raw.rawBuffer.resize(static_cast<std::size_t>(width * height), 0);

    constexpr std::array<float, 3> kSensorValues { 0.20f, 0.40f, 0.40f / 1.5f };
    const float black = raw.metadata.blackLevel;
    const float range = raw.metadata.whiteLevel - black;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool red = (y % 2 == 0) && (x % 2 == 0);
            const bool blue = (y % 2 == 1) && (x % 2 == 1);
            const int color = red ? 0 : (blue ? 2 : 1);
            raw.rawBuffer[static_cast<std::size_t>(y * width + x)] =
                static_cast<std::uint16_t>(std::lround(
                    black + range * kSensorValues[static_cast<std::size_t>(color)]));
        }
    }
    return raw;
}

float ComputeInteriorCfaPhaseSpread(
    const std::vector<float>& rgba,
    int width,
    int height) {
    if (width < 6 || height < 6 ||
        rgba.size() < static_cast<std::size_t>(width * height * 4)) {
        return std::numeric_limits<float>::infinity();
    }

    std::array<std::array<double, 3>, 4> sums {};
    std::array<std::size_t, 4> counts {};
    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {
            const std::size_t phase = static_cast<std::size_t>((y & 1) * 2 + (x & 1));
            const std::size_t pixel = static_cast<std::size_t>((y * width + x) * 4);
            for (int channel = 0; channel < 3; ++channel) {
                sums[phase][static_cast<std::size_t>(channel)] +=
                    rgba[pixel + static_cast<std::size_t>(channel)];
            }
            ++counts[phase];
        }
    }

    float spread = 0.0f;
    for (int channel = 0; channel < 3; ++channel) {
        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        for (std::size_t phase = 0; phase < 4; ++phase) {
            if (counts[phase] == 0) {
                return std::numeric_limits<float>::infinity();
            }
            const float average = static_cast<float>(
                sums[phase][static_cast<std::size_t>(channel)] /
                static_cast<double>(counts[phase]));
            minimum = (std::min)(minimum, average);
            maximum = (std::max)(maximum, average);
        }
        spread = (std::max)(spread, maximum - minimum);
    }
    return spread;
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
    EditorNodeGraph::RawDecodePayload rawDecodePayload;
    rawDecodePayload.settings.exposureStops = 1.25f;
    rawDecodePayload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Manual;
    rawDecodePayload.settings.manualWhiteBalance = { 2.1f, 1.0f, 1.6f };
    rawDecodePayload.settings.highlightMode = Raw::HighlightReconstructionMode::ColorReconstruction;
    rawDecodePayload.settings.highlightStrength = 0.62f;
    rawDecodePayload.settings.highlightThreshold = 0.94f;
    rawDecodePayload.settings.rotationDegrees = 270;
    rawDecodePayload.settings.rotateToFitFrame = true;
    rawDecodePayload.settings.cameraTransformEnabled = true;
    rawDecodePayload.settings.cameraTransformSource = Raw::RawCameraTransformSource::DngForwardMatrix2;

    EditorNodeGraph::Graph rawDecodeGraph;
    EditorNodeGraph::Node* rawDecodeNode =
        rawDecodeGraph.AddRawDecodeNode(rawDecodePayload, EditorNodeGraph::Vec2{ 32.0f, 48.0f });
    const int rawDecodeNodeId = rawDecodeNode ? rawDecodeNode->id : 0;
    const nlohmann::json rawDecodeSerialized =
        EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), rawDecodeGraph);
    EditorNodeGraph::Graph rawDecodeRestoredGraph;
    EditorNodeGraph::DeserializeGraphPayload(rawDecodeSerialized, rawDecodeRestoredGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* restoredRawDecodeNode = rawDecodeRestoredGraph.FindNode(rawDecodeNodeId);
    const bool rawDecodeRoundTripPreserved =
        restoredRawDecodeNode &&
        restoredRawDecodeNode->kind == EditorNodeGraph::NodeKind::RawDecode &&
        std::abs(restoredRawDecodeNode->rawDecode.settings.exposureStops - rawDecodePayload.settings.exposureStops) < 0.0001f &&
        restoredRawDecodeNode->rawDecode.settings.whiteBalanceMode == rawDecodePayload.settings.whiteBalanceMode &&
        std::abs(restoredRawDecodeNode->rawDecode.settings.manualWhiteBalance[0] - rawDecodePayload.settings.manualWhiteBalance[0]) < 0.0001f &&
        std::abs(restoredRawDecodeNode->rawDecode.settings.manualWhiteBalance[2] - rawDecodePayload.settings.manualWhiteBalance[2]) < 0.0001f &&
        restoredRawDecodeNode->rawDecode.settings.highlightMode == rawDecodePayload.settings.highlightMode &&
        std::abs(restoredRawDecodeNode->rawDecode.settings.highlightStrength - rawDecodePayload.settings.highlightStrength) < 0.0001f &&
        std::abs(restoredRawDecodeNode->rawDecode.settings.highlightThreshold - rawDecodePayload.settings.highlightThreshold) < 0.0001f &&
        restoredRawDecodeNode->rawDecode.settings.rotationDegrees == rawDecodePayload.settings.rotationDegrees &&
        restoredRawDecodeNode->rawDecode.settings.rotateToFitFrame == rawDecodePayload.settings.rotateToFitFrame &&
        restoredRawDecodeNode->rawDecode.settings.cameraTransformEnabled == rawDecodePayload.settings.cameraTransformEnabled &&
        restoredRawDecodeNode->rawDecode.settings.cameraTransformSource == rawDecodePayload.settings.cameraTransformSource;

    const bool success =
        graphViewPreserved &&
        preparedPointsPreserved &&
        finalPointsPreserved &&
        rawDecodeRoundTripPreserved;
    if (!success) {
        std::cerr
            << "Develop graph state validation failed:"
            << " graphViewPreserved=" << graphViewPreserved
            << " preparedPointsPreserved=" << preparedPointsPreserved
            << " finalPointsPreserved=" << finalPointsPreserved
            << " rawDecodeRoundTripPreserved=" << rawDecodeRoundTripPreserved
            << " activeGraphView=" << roundTrip.value("activeGraphView", -1)
            << "\n";
    }
    return success;
}

bool ValidateDevelopAutoIntentSerialization() {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
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
    std::string serializedUiMode;
    nlohmann::json developNodeJson;
    for (const nlohmann::json& item : nodesJson) {
        if (item.value("id", 0) == developNodeId) {
            developNodeJson = item;
            serializedIntent = item.value("developAutoGuidance", nlohmann::json::object())
                .value("autoIntent", std::string());
            serializedUiMode = item.value("uiMode", std::string());
            break;
        }
    }

    EditorNodeGraph::Graph restoredGraph;
    EditorNodeGraph::DeserializeGraphPayload(serialized, restoredGraph, 0, {}, 0, 0, 0);
    const EditorNodeGraph::Node* restoredNode = restoredGraph.FindNode(developNodeId);
    const bool roundTripPreserved =
        restoredNode &&
        restoredNode->rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Manual &&
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
    const bool legacyRejected =
        !EditorNodeGraph::IsCurrentGraphPayload(legacySerialized);

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
    const bool unknownRejected =
        !EditorNodeGraph::IsCurrentGraphPayload(unknownSerialized);

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
        EditorNodeGraph::RawDevelopPayload().uiMode == EditorNodeGraph::RawDevelopUiMode::Manual &&
        serializedUiMode == "Auto" &&
        serializedIntent == "PunchyHighContrast" &&
        serializedUserIntentAxes &&
        serializedSubjectImportance &&
        roundTripPreserved &&
        legacyRejected &&
        unknownRejected &&
        interpretedMapViewportState &&
        !developNodeJson.empty();
    if (!success) {
        std::cerr
            << "Develop auto intent serialization validation failed:"
            << " defaultUiModeManual="
            << (EditorNodeGraph::RawDevelopPayload().uiMode == EditorNodeGraph::RawDevelopUiMode::Manual)
            << " serializedUiMode=" << serializedUiMode
            << " serializedIntent=" << serializedIntent
            << " serializedUserIntentAxes=" << serializedUserIntentAxes
            << " serializedSubjectImportance=" << serializedSubjectImportance
            << " roundTripPreserved=" << roundTripPreserved
            << " legacyRejected=" << legacyRejected
            << " unknownRejected=" << unknownRejected
            << " interpretedMapViewportState=" << interpretedMapViewportState
            << " developNodeFound=" << !developNodeJson.empty()
            << "\n";
    }
    return success;
}

bool ValidateDevelopNodeSmoke() {
    constexpr int kRawWidth = 96;
    constexpr int kRawHeight = 64;

    if (!Stack::Validation::ValidateDevelopAutoSolveBehavior()) {
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
    bool demosaicMhcWhiteBalancePhaseStable = false;
    bool profiledMosaicDenoiseChangesNoise = false;
    bool profiledMosaicDenoisePreservesHotPixelMask = false;
    bool rawGpuPreprocessParity = false;
    bool rawGpuPreprocessTelemetryVisible = false;
    bool manualOrientationNonBlank = false;
    bool developGraphBalancedNonBlank = false;
    bool developGraphDarkNonBlank = false;
    bool developGraphHighlightNonBlank = false;
    bool developGraphNoisyNonBlank = false;
    bool developGraphNoisyToneOnlyNonBlank = false;
    bool developGraphDngCalibrationNonBlank = false;
    bool manualRawDecodeChainNonBlank = false;
    bool adoptedMultiFrameRawNonBlank = false;
    bool developGraphRawStageCacheReuseObserved = false;
    bool developGraphPreFinishStageCacheReuseObserved = false;
    bool noisyCombinedToneNotCollapsed = false;
    float balancedMaxRgb = 0.0f;
    float darkMaxRgb = 0.0f;
    float highlightMaxRgb = 0.0f;
    float demosaicMhcWhiteBalancePhaseSpread = std::numeric_limits<float>::infinity();
    float rawGpuNormalizedMaxError = std::numeric_limits<float>::infinity();
    float rawGpuVarianceMaxError = std::numeric_limits<float>::infinity();
    float manualOrientationMaxRgb = 0.0f;
    float developGraphBalancedMaxRgb = 0.0f;
    float developGraphDarkMaxRgb = 0.0f;
    float developGraphHighlightMaxRgb = 0.0f;
    float developGraphNoisyMaxRgb = 0.0f;
    float developGraphNoisyToneOnlyMaxRgb = 0.0f;
    float developGraphDngCalibrationMaxRgb = 0.0f;
    float manualRawDecodeChainMaxRgb = 0.0f;
    float adoptedMultiFrameRawMaxRgb = 0.0f;
    float developGraphNoisyAvgLuma = 0.0f;
    float developGraphNoisyToneOnlyAvgLuma = 0.0f;

    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Develop smoke validation failed: unable to load OpenGL functions.\n";
    } else {
        std::cout << "Develop smoke: RAW GPU preprocess parity..." << std::endl;
        rawGpuPreprocessParity = ValidateRawGpuPreprocessParity(
            rawGpuNormalizedMaxError,
            rawGpuVarianceMaxError);
        std::cout << "Develop smoke: direct RAW pipeline..." << std::endl;
        Raw::RawGpuPipeline rawPipeline;
        const Raw::RawImageData balancedRaw = BuildSyntheticRawScene(SyntheticRawScene::Balanced, kRawWidth, kRawHeight);
        const Raw::RawImageData darkRaw = BuildSyntheticRawScene(SyntheticRawScene::DarkMid, kRawWidth, kRawHeight);
        const Raw::RawImageData highlightRaw = BuildSyntheticRawScene(SyntheticRawScene::HighlightHeavy, kRawWidth, kRawHeight);
        Raw::RawImageData noisyRaw = BuildSyntheticRawScene(SyntheticRawScene::NoisyLowLight, kRawWidth, kRawHeight);
        noisyRaw.metadata.hasDngNoiseProfile = true;
        noisyRaw.metadata.dngNoiseProfile = {
            Raw::DngNoiseProfilePlane { 0.004, 0.00004 }
        };
        const Raw::RawImageData whiteBalanceFlatRaw =
            BuildSyntheticWhiteBalanceFlat(kRawWidth, kRawHeight);
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

        Raw::RawDevelopSettings mhcWhiteBalanceSettings;
        mhcWhiteBalanceSettings.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
        mhcWhiteBalanceSettings.whiteBalanceMode = Raw::WhiteBalanceMode::Manual;
        mhcWhiteBalanceSettings.manualWhiteBalance = { 2.0f, 1.0f, 1.5f };
        mhcWhiteBalanceSettings.demosaicMethod = Raw::DemosaicMethod::MalvarHeCutler;
        mhcWhiteBalanceSettings.cameraTransformEnabled = false;
        mhcWhiteBalanceSettings.highlightMode = Raw::HighlightReconstructionMode::Off;
        mhcWhiteBalanceSettings.falseColorSuppression = 0.0f;
        mhcWhiteBalanceSettings.defringeStrength = 0.0f;
        mhcWhiteBalanceSettings.highlightEdgeCleanup = 0.0f;
        const unsigned int mhcWhiteBalanceTexture =
            rawPipeline.Render(whiteBalanceFlatRaw, mhcWhiteBalanceSettings);
        const int mhcWhiteBalanceWidth = rawPipeline.GetOutputWidth();
        const int mhcWhiteBalanceHeight = rawPipeline.GetOutputHeight();
        const std::vector<float> mhcWhiteBalancePixels =
            ReadTextureRgbaFloat(
                mhcWhiteBalanceTexture,
                mhcWhiteBalanceWidth,
                mhcWhiteBalanceHeight);
        demosaicMhcWhiteBalancePhaseSpread =
            ComputeInteriorCfaPhaseSpread(
                mhcWhiteBalancePixels,
                mhcWhiteBalanceWidth,
                mhcWhiteBalanceHeight);
        demosaicMhcWhiteBalancePhaseStable =
            mhcWhiteBalanceTexture != 0 &&
            mhcWhiteBalanceWidth == kRawWidth &&
            mhcWhiteBalanceHeight == kRawHeight &&
            demosaicMhcWhiteBalancePhaseSpread < 0.002f;

        Raw::RawDevelopSettings profiledDenoiseSettings;
        profiledDenoiseSettings.processingVersion =
            Raw::RawProcessingVersion::TruthfulV2;
        profiledDenoiseSettings.cameraTransformEnabled = false;
        profiledDenoiseSettings.demosaicMethod =
            Raw::DemosaicMethod::Bilinear;
        profiledDenoiseSettings.debugView =
            Raw::RawDebugView::PostDenoiseMosaic;
        profiledDenoiseSettings.mosaicDenoise.enabled = true;
        profiledDenoiseSettings.mosaicDenoise.mode =
            Raw::RawMosaicDenoiseMode::DngNoiseProfile;
        profiledDenoiseSettings.mosaicDenoise.hotPixelSuppression = false;
        profiledDenoiseSettings.mosaicDenoise.lumaStrength = 1.0f;
        profiledDenoiseSettings.mosaicDenoise.chromaStrength = 1.0f;
        profiledDenoiseSettings.mosaicDenoise.radius = 2;
        profiledDenoiseSettings.mosaicDenoise.edgeProtection = 0.55f;
        profiledDenoiseSettings.mosaicDenoise.iterations = 1;
        Raw::RawDevelopSettings denoiseOffSettings =
            profiledDenoiseSettings;
        denoiseOffSettings.mosaicDenoise.enabled = false;
        const unsigned int denoiseOffTexture =
            rawPipeline.Render(noisyRaw, denoiseOffSettings);
        const int denoiseWidth = rawPipeline.GetOutputWidth();
        const int denoiseHeight = rawPipeline.GetOutputHeight();
        const std::vector<float> denoiseOffPixels =
            ReadTextureRgbaFloat(
                denoiseOffTexture,
                denoiseWidth,
                denoiseHeight);
        const unsigned int profiledDenoiseTexture =
            rawPipeline.Render(noisyRaw, profiledDenoiseSettings);
        const std::vector<float> profiledDenoisePixels =
            ReadTextureRgbaFloat(
                profiledDenoiseTexture,
                rawPipeline.GetOutputWidth(),
                rawPipeline.GetOutputHeight());
        double denoiseDifferenceSum = 0.0;
        std::size_t denoiseDifferenceSamples = 0;
        if (denoiseOffPixels.size() == profiledDenoisePixels.size()) {
            for (std::size_t i = 0; i + 3 < denoiseOffPixels.size(); i += 4) {
                for (int channel = 0; channel < 3; ++channel) {
                    denoiseDifferenceSum += std::abs(
                        static_cast<double>(
                            profiledDenoisePixels[
                                i + static_cast<std::size_t>(channel)]) -
                        static_cast<double>(
                            denoiseOffPixels[
                                i + static_cast<std::size_t>(channel)]));
                    ++denoiseDifferenceSamples;
                }
            }
        }
        profiledMosaicDenoiseChangesNoise =
            profiledDenoiseTexture != 0 &&
            denoiseDifferenceSamples > 0 &&
            denoiseDifferenceSum /
                    static_cast<double>(denoiseDifferenceSamples) >
                1.0e-6;

        Raw::RawDevelopSettings profiledHotPixelSettings =
            profiledDenoiseSettings;
        profiledHotPixelSettings.debugView = Raw::RawDebugView::HotPixelMask;
        profiledHotPixelSettings.mosaicDenoise.hotPixelSuppression = true;
        const unsigned int profiledHotPixelTexture =
            rawPipeline.Render(noisyRaw, profiledHotPixelSettings);
        const std::vector<float> profiledHotPixelPixels =
            ReadTextureRgbaFloat(
                profiledHotPixelTexture,
                rawPipeline.GetOutputWidth(),
                rawPipeline.GetOutputHeight());
        Raw::RawDevelopSettings legacyHotPixelSettings =
            profiledHotPixelSettings;
        legacyHotPixelSettings.mosaicDenoise.mode =
            Raw::RawMosaicDenoiseMode::FixedThreshold;
        const unsigned int legacyHotPixelTexture =
            rawPipeline.Render(noisyRaw, legacyHotPixelSettings);
        const std::vector<float> legacyHotPixelPixels =
            ReadTextureRgbaFloat(
                legacyHotPixelTexture,
                rawPipeline.GetOutputWidth(),
                rawPipeline.GetOutputHeight());
        float hotPixelDifference = 0.0f;
        if (profiledHotPixelPixels.size() == legacyHotPixelPixels.size()) {
            for (std::size_t i = 0; i < profiledHotPixelPixels.size(); ++i) {
                hotPixelDifference = std::max(
                    hotPixelDifference,
                    std::abs(
                        profiledHotPixelPixels[i] -
                        legacyHotPixelPixels[i]));
            }
        } else {
            hotPixelDifference = std::numeric_limits<float>::infinity();
        }
        profiledMosaicDenoisePreservesHotPixelMask =
            profiledHotPixelTexture != 0 &&
            legacyHotPixelTexture != 0 &&
            hotPixelDifference <= 1.0e-7f;

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

        auto graphPipeline = std::make_unique<RenderPipeline>();
        graphPipeline->Initialize();
        graphPipeline->Resize(kRawWidth, kRawHeight);
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

            graphPipeline->Resize(kRawWidth, kRawHeight);
            graphPipeline->ExecuteGraph(graph);
            if (outRawBaseCacheHit) {
                *outRawBaseCacheHit = graphPipeline->WasGraphImageCacheHit(2, "__rawDevelopBase");
            }
            if (outPreFinishCacheHit) {
                *outPreFinishCacheHit = graphPipeline->WasGraphImageCacheHit(
                    2,
                    EditorNodeGraph::kPreFinishImageOutputSocketId);
            }
            outMaxRgb = ReadTextureMaxRgb(
                graphPipeline->GetOutputTexture(),
                graphPipeline->GetCanvasWidth(),
                graphPipeline->GetCanvasHeight());
            int outputW = 0;
            int outputH = 0;
            const std::vector<unsigned char> outputPixels = graphPipeline->GetOutputPixels(outputW, outputH);
            if (outAvgLuma) {
                *outAvgLuma = ComputeAverageNormalizedLuma(outputPixels);
            }
            return graphPipeline->GetOutputTexture() != 0 &&
                outputW == kRawWidth &&
                outputH == kRawHeight &&
                !outputPixels.empty() &&
                outMaxRgb > 0.01f;
        };
        auto runManualRawGraph = [&](const Raw::RawImageData& raw,
                                     const EditorNodeGraph::RawDecodePayload& decodePayload,
                                     const nlohmann::json& toneCurveJson,
                                     std::uint64_t requestRevision,
                                     float& outMaxRgb) {
            RenderGraphSnapshot graph;
            graph.outputNodeId = 5;

            RenderGraphNode rawSourceNode;
            rawSourceNode.nodeId = 1;
            rawSourceNode.kind = RenderGraphNodeKind::RawSource;
            rawSourceNode.requestRevision = requestRevision;
            rawSourceNode.rawSource.metadata = raw.metadata;
            rawSourceNode.rawSource.embeddedRawData = raw;
            graph.nodes.push_back(std::move(rawSourceNode));

            RenderGraphNode rawDecodeNode;
            rawDecodeNode.nodeId = 2;
            rawDecodeNode.kind = RenderGraphNodeKind::RawDecode;
            rawDecodeNode.requestRevision = requestRevision;
            rawDecodeNode.rawDecode.settings = decodePayload.settings;
            graph.nodes.push_back(std::move(rawDecodeNode));

            RenderGraphNode toneCurveNode;
            toneCurveNode.nodeId = 3;
            toneCurveNode.kind = RenderGraphNodeKind::Layer;
            toneCurveNode.requestRevision = requestRevision;
            toneCurveNode.layerJson = toneCurveJson;
            graph.nodes.push_back(std::move(toneCurveNode));

            ViewTransformLayer viewTransformLayer;
            RenderGraphNode viewTransformNode;
            viewTransformNode.nodeId = 4;
            viewTransformNode.kind = RenderGraphNodeKind::Layer;
            viewTransformNode.requestRevision = requestRevision;
            viewTransformNode.layerJson = viewTransformLayer.Serialize();
            graph.nodes.push_back(std::move(viewTransformNode));

            RenderGraphNode outputNode;
            outputNode.nodeId = 5;
            outputNode.kind = RenderGraphNodeKind::Output;
            outputNode.requestRevision = requestRevision;
            graph.nodes.push_back(std::move(outputNode));

            graph.links.push_back(RenderGraphLink{ 1, "rawOut", 2, "rawIn" });
            graph.links.push_back(RenderGraphLink{ 2, "imageOut", 3, "imageIn" });
            graph.links.push_back(RenderGraphLink{ 3, "imageOut", 4, "imageIn" });
            graph.links.push_back(RenderGraphLink{ 4, "imageOut", 5, "imageIn" });

            graphPipeline->Resize(kRawWidth, kRawHeight);
            graphPipeline->ExecuteGraph(graph);
            outMaxRgb = ReadTextureMaxRgb(
                graphPipeline->GetOutputTexture(),
                graphPipeline->GetCanvasWidth(),
                graphPipeline->GetCanvasHeight());
            int outputW = 0;
            int outputH = 0;
            const std::vector<unsigned char> outputPixels = graphPipeline->GetOutputPixels(outputW, outputH);
            return graphPipeline->GetOutputTexture() != 0 &&
                outputW == kRawWidth &&
                outputH == kRawHeight &&
                !outputPixels.empty() &&
                outMaxRgb > 0.01f;
        };
        developGraphBalancedNonBlank =
            runDevelopGraph(balancedRaw, neutralPayload, 101, developGraphBalancedMaxRgb, nullptr);
        {
            const GraphExecutionStats& rawStats =
                graphPipeline->GetLastGraphExecutionStats();
            rawGpuPreprocessTelemetryVisible =
                rawStats.rawGpuPreprocessDispatches > 0 &&
                rawStats.rawSensorUploadBytes > 0u &&
                rawStats.rawMetadataUploadBytes > 0u &&
                rawStats.rawCorrectedUploadBytes == 0u &&
                rawStats.rawVarianceUploadBytes == 0u &&
                rawStats.rawCpuPreprocessFallbacks == 0;
        }
        std::cout << "Develop smoke: first graph RAW render complete." << std::endl;
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
        {
            RenderPipeline adoptedPipeline;
            adoptedPipeline.Initialize();
            adoptedPipeline.Resize(kRawWidth, kRawHeight);
            RenderGraphSnapshot graph;
            graph.outputNodeId = 12;

            RenderGraphNode adoptedRawNode;
            adoptedRawNode.nodeId = 11;
            adoptedRawNode.kind = RenderGraphNodeKind::RawProjectSourceSet;
            adoptedRawNode.requestRevision = 107;
            adoptedRawNode.rawProjectSourceSet.sourceSetId =
                "synthetic-hdr-source-set";
            adoptedRawNode.rawProjectSourceSet.inputRevision = 7;
            adoptedRawNode.rawProjectSourceSet.contentHash = 0x1234u;
            adoptedRawNode.rawProjectSourceSet.resultAvailable = true;
            adoptedRawNode.rawDevelopment.recipe =
                Stack::RawRecipe::MakeDefaultRecipe(
                    "hdr://synthetic-project/synthetic-hdr-source-set",
                    "Synthetic HDR result");
            adoptedRawNode.rawDevelopment.recipe.technical.processingVersion =
                Raw::RawProcessingVersion::TruthfulV2;
            adoptedRawNode.rawDevelopment.recipe.technical
                .applyBaselineExposure = false;
            adoptedRawNode.rawDevelopment.recipe.colorWarp.enabled = true;
            Stack::RawRecipe::RawColorWarpPin adoptedColorPin;
            adoptedColorPin.id = "synthetic-spatial-color";
            adoptedColorPin.sourceA = 0.0f;
            adoptedColorPin.sourceB = 0.0f;
            adoptedColorPin.targetA = 0.04f;
            adoptedColorPin.targetB = 0.025f;
            adoptedColorPin.radius = 0.75f;
            adoptedColorPin.regionId = "synthetic-spatial-region";
            adoptedColorPin.evCurve =
                Stack::RawRecipe::MakeColorWarpEvCurveHump(
                    -1.0f, 7.0f, 1.0f);
            Stack::RawRecipe::RawColorWarpRegion adoptedColorRegion;
            adoptedColorRegion.id = adoptedColorPin.regionId;
            adoptedColorRegion.spatialMode =
                Stack::RawRecipe::RawColorWarpSpatialMode::Cohesive;
            adoptedColorRegion.featherPixels = 2.0f;
            adoptedColorRegion.circles.push_back({
                "synthetic-spatial-circle", 0.5f, 0.5f, 0.35f, 0.35f,
                Stack::RawRecipe::RawColorWarpInterpretationMode::GuidedFamily,
                Stack::RawRecipe::RawColorWarpSamplePolarity::Include
            });
            adoptedRawNode.rawDevelopment.recipe.colorWarp.pins = {
                adoptedColorPin
            };
            adoptedRawNode.rawDevelopment.recipe.colorWarp.regions = {
                adoptedColorRegion
            };
            adoptedRawNode.rawDevelopment.embeddedRawData =
                std::make_shared<const Raw::RawImageData>(balancedRaw);
            graph.nodes.push_back(std::move(adoptedRawNode));

            RenderGraphNode outputNode;
            outputNode.nodeId = 12;
            outputNode.kind = RenderGraphNodeKind::Output;
            outputNode.requestRevision = 107;
            graph.nodes.push_back(std::move(outputNode));
            graph.links.push_back(RenderGraphLink{
                11,
                EditorNodeGraph::kImageOutputSocketId,
                12,
                EditorNodeGraph::kImageInputSocketId });

            adoptedPipeline.ExecuteGraph(graph);
            adoptedMultiFrameRawMaxRgb = ReadTextureMaxRgb(
                adoptedPipeline.GetOutputTexture(),
                adoptedPipeline.GetCanvasWidth(),
                adoptedPipeline.GetCanvasHeight());
            int outputWidth = 0;
            int outputHeight = 0;
            const std::vector<unsigned char> outputPixels =
                adoptedPipeline.GetOutputPixels(outputWidth, outputHeight);
            adoptedMultiFrameRawNonBlank =
                adoptedPipeline.GetOutputTexture() != 0 &&
                outputWidth == kRawWidth &&
                outputHeight == kRawHeight &&
                !outputPixels.empty() &&
                adoptedMultiFrameRawMaxRgb > 0.01f;
        }
        EditorNodeGraph::RawDecodePayload manualRawDecodePayload;
        manualRawDecodePayload.settings = neutralPayload.settings;
        ToneCurveLayer manualToneCurveLayer;
        manualRawDecodeChainNonBlank = runManualRawGraph(
            balancedRaw,
            manualRawDecodePayload,
            manualToneCurveLayer.Serialize(),
            109,
            manualRawDecodeChainMaxRgb);

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
            demosaicMhcWhiteBalancePhaseStable &&
            rawGpuPreprocessParity &&
            rawGpuPreprocessTelemetryVisible &&
            profiledMosaicDenoiseChangesNoise &&
            profiledMosaicDenoisePreservesHotPixelMask &&
            manualOrientationNonBlank &&
            developGraphBalancedNonBlank &&
            developGraphDarkNonBlank &&
            developGraphHighlightNonBlank &&
            developGraphDngCalibrationNonBlank &&
            manualRawDecodeChainNonBlank &&
            adoptedMultiFrameRawNonBlank &&
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
            << " demosaicMhcWhiteBalancePhaseStable=" << demosaicMhcWhiteBalancePhaseStable
            << " demosaicMhcWhiteBalancePhaseSpread=" << demosaicMhcWhiteBalancePhaseSpread
            << " rawGpuPreprocessParity=" << rawGpuPreprocessParity
            << " rawGpuPreprocessTelemetryVisible=" << rawGpuPreprocessTelemetryVisible
            << " rawGpuNormalizedMaxError=" << rawGpuNormalizedMaxError
            << " rawGpuVarianceMaxError=" << rawGpuVarianceMaxError
            << " profiledMosaicDenoiseChangesNoise=" << profiledMosaicDenoiseChangesNoise
            << " profiledMosaicDenoisePreservesHotPixelMask=" << profiledMosaicDenoisePreservesHotPixelMask
            << " manualOrientationNonBlank=" << manualOrientationNonBlank
            << " developGraphBalancedNonBlank=" << developGraphBalancedNonBlank
            << " developGraphDarkNonBlank=" << developGraphDarkNonBlank
            << " developGraphHighlightNonBlank=" << developGraphHighlightNonBlank
            << " developGraphNoisyNonBlank=" << developGraphNoisyNonBlank
            << " developGraphNoisyToneOnlyNonBlank=" << developGraphNoisyToneOnlyNonBlank
            << " developGraphDngCalibrationNonBlank=" << developGraphDngCalibrationNonBlank
            << " manualRawDecodeChainNonBlank=" << manualRawDecodeChainNonBlank
            << " adoptedMultiFrameRawNonBlank=" << adoptedMultiFrameRawNonBlank
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
            << " manualRawDecodeChainMaxRgb=" << manualRawDecodeChainMaxRgb
            << " adoptedMultiFrameRawMaxRgb=" << adoptedMultiFrameRawMaxRgb
            << " developGraphNoisyAvgLuma=" << developGraphNoisyAvgLuma
            << " developGraphNoisyToneOnlyAvgLuma=" << developGraphNoisyToneOnlyAvgLuma
            << "\n";
    } else {
        std::cout << "Develop node smoke validation passed." << std::endl;
    }

    return success;
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
        std::cout << "Develop real RAW smoke: graph pipeline..." << std::endl;
        auto graphPipeline = std::make_unique<RenderPipeline>();
        graphPipeline->Initialize();
        graphPipeline->SetPreviewMaxDimension(1024);
        graphPipeline->Resize(64, 64);

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

            graphPipeline->Resize(64, 64);
            graphPipeline->ExecuteGraph(graph);
            const int outputW = graphPipeline->GetCanvasWidth();
            const int outputH = graphPipeline->GetCanvasHeight();
            outMaxRgb = ReadTextureMaxRgb(graphPipeline->GetOutputTexture(), outputW, outputH);
            int pixelW = 0;
            int pixelH = 0;
            const std::vector<unsigned char> outputPixels = graphPipeline->GetOutputPixels(pixelW, pixelH);
            if (outPixels) {
                *outPixels = outputPixels;
            }
            if (outPixelW) {
                *outPixelW = pixelW;
            }
            if (outPixelH) {
                *outPixelH = pixelH;
            }
            outFeedbacks = graphPipeline->GetToneCurveAutoRewriteFeedback();
            return graphPipeline->GetOutputTexture() != 0 &&
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
            auto rawWorkspacePipeline = std::make_unique<RenderPipeline>();
            rawWorkspacePipeline->Initialize();
            rawWorkspacePipeline->SetPreviewMaxDimension(1024);
            rawWorkspacePipeline->Resize(64, 64);
            Stack::RawRecipe::RawDevelopmentRecipe rawWorkspaceRecipe =
                Stack::RawRecipe::MakeDefaultRecipe(
                    path.string(),
                    path.filename().string());
            rawWorkspaceRecipe.technical.processingVersion =
                Raw::RawProcessingVersion::TruthfulV2;
            rawWorkspaceRecipe.technical.demosaicMethod =
                Raw::DemosaicMethod::MalvarHeCutler;
            rawWorkspaceRecipe.preToneExposureEv = 0.25f;
            // Exercise the direct-target Local Range shader and its
            // selected-component qualifier in the real-RAW GL smoke path.
            // A broad tonal lobe keeps the center seed valid across the
            // heterogeneous validation corpus.
            rawWorkspaceRecipe.localRange.enabled = true;
            Stack::RawRecipe::RawLocalRangeTargetZone smokeTargetZone;
            smokeTargetZone.id = "real-raw-smoke-target";
            smokeTargetZone.name = "Smoke target";
            smokeTargetZone.centerEv = 0.0f;
            smokeTargetZone.coreHalfWidthEv = 4.0f;
            smokeTargetZone.featherEv = 4.0f;
            smokeTargetZone.deltaEv = 0.20f;
            smokeTargetZone.scope =
                Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
            smokeTargetZone.seeds.push_back({ 0.5f, 0.5f });
            rawWorkspaceRecipe.localRange.targetZones.push_back(
                std::move(smokeTargetZone));

            RawDevelopmentGraphScopeStage requestedGraphScopeStage =
                RawDevelopmentGraphScopeStage::None;
            int requestedGraphScopeMaxDimension = 0;
            RawDevelopmentGradingScopeSource requestedGradingScopeSource =
                RawDevelopmentGradingScopeSource::None;
            int requestedGradingScopeMaxDimension = 0;
            bool requestedGradingScopePixels = true;

            auto runRawWorkspaceGraph =
                [&](const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
                    bool analysisEnabled,
                    int stageImageReadbackMaxDimension,
                    std::uint64_t revision,
                    std::vector<unsigned char>& outPixels,
                    std::vector<float>* outFloatPixels,
                    GraphExecutionStats& outGraphStats,
                    double& outMilliseconds) {
                    RenderGraphSnapshot graph;
                    graph.outputNodeId = 2;

                    RenderGraphNode rawDevelopmentNode;
                    rawDevelopmentNode.nodeId = 1;
                    rawDevelopmentNode.kind = RenderGraphNodeKind::RawDevelopment;
                    rawDevelopmentNode.requestRevision = revision;
                    rawDevelopmentNode.rawDevelopment.recipe = recipe;
                    graph.nodes.push_back(std::move(rawDevelopmentNode));

                    RenderGraphNode outputNode;
                    outputNode.nodeId = 2;
                    outputNode.kind = RenderGraphNodeKind::Output;
                    outputNode.requestRevision = revision;
                    graph.nodes.push_back(std::move(outputNode));
                    graph.links.push_back(RenderGraphLink{ 1, "imageOut", 2, "imageIn" });

                    rawWorkspacePipeline->SetRawDevelopmentAnalysisEnabled(analysisEnabled);
                    rawWorkspacePipeline->SetRawDevelopmentStageImageReadbackMaxDimension(
                        stageImageReadbackMaxDimension);
                    rawWorkspacePipeline->SetRawDevelopmentGraphScopeReadbackRequest(
                        requestedGraphScopeStage,
                        requestedGraphScopeMaxDimension);
                    rawWorkspacePipeline->SetRawDevelopmentGradingScopeReadbackRequest(
                        requestedGradingScopeSource,
                        requestedGradingScopeMaxDimension,
                        revision,
                        path.string(), requestedGradingScopePixels);
                    const auto begin = std::chrono::steady_clock::now();
                    rawWorkspacePipeline->ExecuteGraph(graph);
                    glFinish();
                    outMilliseconds = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - begin).count();
                    outGraphStats = rawWorkspacePipeline->GetLastGraphExecutionStats();
                    if (outFloatPixels) {
                        *outFloatPixels = ReadTextureRgbaFloat(
                            rawWorkspacePipeline->GetOutputTexture(),
                            rawWorkspacePipeline->GetCanvasWidth(),
                            rawWorkspacePipeline->GetCanvasHeight());
                    }
                    int pixelW = 0;
                    int pixelH = 0;
                    outPixels = rawWorkspacePipeline->GetOutputPixels(pixelW, pixelH);
                    return rawWorkspacePipeline->GetOutputTexture() != 0 &&
                        pixelW > 0 &&
                        pixelH > 0 &&
                        !outPixels.empty();
                };

            struct RawWorkspaceTargetSampleProbe {
                bool renderValid = false;
                bool sampleValid = false;
                bool overlayTextureValid = false;
                bool publishedTextureValid = false;
                float sceneEv = 0.0f;
                float sceneLuma = 0.0f;
                float sampleU = 0.0f;
                float sampleV = 0.0f;
                std::array<float, 3> sceneRgb { 0.0f, 0.0f, 0.0f };
                std::uint32_t authoredZoneHitBits = 0;
                float strongestAuthoredZoneWeight = 0.0f;
                float publishedMaxRgb = 0.0f;
                std::vector<unsigned char> outputPixels;
            };

            auto runRawWorkspaceTargetSample =
                [&](float requestU,
                    float requestV,
                    std::uint64_t revision) {
                    RawWorkspaceTargetSampleProbe probe;

                    RenderGraphSnapshot graph;
                    graph.outputNodeId = 2;
                    graph.rawWorkspaceLocalRangeOverlayMode = "region-mask";
                    graph.rawWorkspaceLocalRangeTargetSampleRequested = true;
                    graph.rawWorkspaceLocalRangeTargetSampleU = requestU;
                    graph.rawWorkspaceLocalRangeTargetSampleV = requestV;

                    RenderGraphNode rawDevelopmentNode;
                    rawDevelopmentNode.nodeId = 1;
                    rawDevelopmentNode.kind = RenderGraphNodeKind::RawDevelopment;
                    rawDevelopmentNode.requestRevision = revision;
                    rawDevelopmentNode.rawDevelopment.recipe = rawWorkspaceRecipe;
                    graph.nodes.push_back(std::move(rawDevelopmentNode));

                    RenderGraphNode outputNode;
                    outputNode.nodeId = 2;
                    outputNode.kind = RenderGraphNodeKind::Output;
                    outputNode.requestRevision = revision;
                    graph.nodes.push_back(std::move(outputNode));
                    graph.links.push_back(RenderGraphLink{ 1, "imageOut", 2, "imageIn" });

                    rawWorkspacePipeline->SetRawDevelopmentAnalysisEnabled(false);
                    rawWorkspacePipeline->SetRawDevelopmentStageImageReadbackMaxDimension(0);
                    rawWorkspacePipeline->ExecuteGraph(graph);
                    glFinish();

                    probe.sampleValid =
                        rawWorkspacePipeline->GetRawDevelopmentLocalRangeTargetSample(
                            probe.sceneEv,
                            probe.sceneLuma,
                            probe.sampleU,
                            probe.sampleV,
                            &probe.sceneRgb,
                            &probe.authoredZoneHitBits,
                            &probe.strongestAuthoredZoneWeight);

                    int pixelW = 0;
                    int pixelH = 0;
                    probe.outputPixels =
                        rawWorkspacePipeline->GetOutputPixels(pixelW, pixelH);
                    probe.renderValid =
                        rawWorkspacePipeline->GetOutputTexture() != 0 &&
                        pixelW > 0 &&
                        pixelH > 0 &&
                        !probe.outputPixels.empty();

                    int overlayW = 0;
                    int overlayH = 0;
                    const unsigned int overlayTexture =
                        rawWorkspacePipeline->TakeRawDevelopmentLocalRangeOverlayTexture(
                            overlayW,
                            overlayH);
                    probe.overlayTextureValid =
                        overlayTexture != 0 &&
                        glIsTexture(overlayTexture) == GL_TRUE &&
                        overlayW == pixelW &&
                        overlayH == pixelH;
                    if (overlayTexture != 0) {
                        glDeleteTextures(1, &overlayTexture);
                    }

                    int publishedW = 0;
                    int publishedH = 0;
                    const unsigned int publishedTexture =
                        rawWorkspacePipeline->PublishSharedOutputTexture(
                            publishedW,
                            publishedH,
                            true);
                    glFinish();
                    if (publishedTexture != 0 &&
                        glIsTexture(publishedTexture) == GL_TRUE &&
                        publishedW > 0 &&
                        publishedH > 0) {
                        probe.publishedMaxRgb =
                            ReadTextureMaxRgb(
                                publishedTexture,
                                publishedW,
                                publishedH);
                    }
                    probe.publishedTextureValid =
                        publishedTexture != 0 &&
                        glIsTexture(publishedTexture) == GL_TRUE &&
                        publishedW == pixelW &&
                        publishedH == pixelH &&
                        std::isfinite(probe.publishedMaxRgb) &&
                        probe.publishedMaxRgb > 0.00001f;
                    if (publishedTexture != 0) {
                        glDeleteTextures(1, &publishedTexture);
                    }

                    return probe;
                };

            std::vector<unsigned char> rawWorkspaceWarmPixels;
            std::vector<unsigned char> rawWorkspaceAnalyzedPixels;
            std::vector<unsigned char> rawWorkspaceInteractivePixels;
            std::vector<unsigned char> rawWorkspaceDownstreamPixels;
            std::vector<unsigned char> rawWorkspaceUpstreamPixels;
            std::vector<unsigned char> rawWorkspaceTargetEditPixels;
            std::vector<float> rawWorkspaceAnalyzedFloatPixels;
            std::vector<float> rawWorkspaceInteractiveFloatPixels;
            GraphExecutionStats rawWorkspaceWarmStats;
            GraphExecutionStats rawWorkspaceAnalyzedStats;
            GraphExecutionStats rawWorkspaceInteractiveStats;
            GraphExecutionStats rawWorkspaceDownstreamStats;
            GraphExecutionStats rawWorkspaceUpstreamStats;
            GraphExecutionStats rawWorkspaceTargetEditStats;
            double rawWorkspaceWarmMs = 0.0;
            double rawWorkspaceAnalyzedMs = 0.0;
            double rawWorkspaceInteractiveMs = 0.0;
            double rawWorkspaceDownstreamMs = 0.0;
            double rawWorkspaceUpstreamMs = 0.0;
            double rawWorkspaceTargetEditMs = 0.0;
            const bool rawWorkspaceWarmOk = runRawWorkspaceGraph(
                rawWorkspaceRecipe,
                true,
                0,
                requestBase + 100,
                rawWorkspaceWarmPixels,
                nullptr,
                rawWorkspaceWarmStats,
                rawWorkspaceWarmMs);
            const int uncroppedWorkspaceWidth =
                rawWorkspacePipeline->GetCanvasWidth();
            const int uncroppedWorkspaceHeight =
                rawWorkspacePipeline->GetCanvasHeight();
            int tiledExportWidth = 0;
            int tiledExportHeight = 0;
            const std::vector<unsigned char> tiledExportPixels =
                rawWorkspacePipeline->GetOutputPixelsTiledPbo(
                    tiledExportWidth,
                    tiledExportHeight,
                    73);
            const bool tiledExportReadbackOk =
                rawWorkspaceWarmOk &&
                tiledExportWidth == uncroppedWorkspaceWidth &&
                tiledExportHeight == uncroppedWorkspaceHeight &&
                tiledExportPixels == rawWorkspaceWarmPixels;
            success = success && tiledExportReadbackOk;
            std::cout
                << "RAW worker tiled export readback: "
                << path.filename().string()
                << " output=" << tiledExportWidth << 'x'
                << tiledExportHeight
                << " passed=" << tiledExportReadbackOk
                << "\n";
            if (!tiledExportReadbackOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: tiled "
                       "PBO export readback diverged from monolithic output for "
                    << path.string() << "\n";
            }

            Stack::RawRecipe::RawDevelopmentRecipe croppedWorkspaceRecipe =
                rawWorkspaceRecipe;
            croppedWorkspaceRecipe.cropRotation.cropEnabled = true;
            croppedWorkspaceRecipe.cropRotation.cropX = 0.25f;
            croppedWorkspaceRecipe.cropRotation.cropY = 0.25f;
            croppedWorkspaceRecipe.cropRotation.cropWidth = 0.50f;
            croppedWorkspaceRecipe.cropRotation.cropHeight = 0.50f;
            std::vector<unsigned char> croppedWorkspacePixels;
            GraphExecutionStats croppedWorkspaceStats;
            double croppedWorkspaceMs = 0.0;
            const bool croppedWorkspaceOk = runRawWorkspaceGraph(
                croppedWorkspaceRecipe,
                false,
                0,
                requestBase + 131,
                croppedWorkspacePixels,
                nullptr,
                croppedWorkspaceStats,
                croppedWorkspaceMs);
            const int croppedWorkspaceWidth =
                rawWorkspacePipeline->GetCanvasWidth();
            const int croppedWorkspaceHeight =
                rawWorkspacePipeline->GetCanvasHeight();
            const int expectedCroppedWidth =
                static_cast<int>(std::ceil(
                    0.75f * static_cast<float>(uncroppedWorkspaceWidth))) -
                static_cast<int>(std::floor(
                    0.25f * static_cast<float>(uncroppedWorkspaceWidth)));
            const int expectedCroppedHeight =
                static_cast<int>(std::ceil(
                    0.75f * static_cast<float>(uncroppedWorkspaceHeight))) -
                static_cast<int>(std::floor(
                    0.25f * static_cast<float>(uncroppedWorkspaceHeight)));

            const bool cropSemanticsContractOk =
                rawWorkspaceWarmOk &&
                croppedWorkspaceOk &&
                croppedWorkspaceWidth == expectedCroppedWidth &&
                croppedWorkspaceHeight == expectedCroppedHeight;
            success = success && cropSemanticsContractOk;
            std::cout
                << "RAW workspace crop semantics: "
                << path.filename().string()
                << " source=" << uncroppedWorkspaceWidth << 'x'
                << uncroppedWorkspaceHeight
                << " cropped=" << croppedWorkspaceWidth << 'x'
                << croppedWorkspaceHeight
                << " expected=" << expectedCroppedWidth << 'x'
                << expectedCroppedHeight
                << " passed=" << cropSemanticsContractOk
                << "\n";
            if (!cropSemanticsContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: crop "
                       "geometry failed for "
                    << path.string() << "\n";
            }
            requestedGraphScopeStage =
                RawDevelopmentGraphScopeStage::LocalRangeInput;
            requestedGraphScopeMaxDimension = 64;
            const bool rawWorkspaceAnalyzedOk = runRawWorkspaceGraph(
                rawWorkspaceRecipe,
                true,
                1,
                requestBase + 101,
                rawWorkspaceAnalyzedPixels,
                &rawWorkspaceAnalyzedFloatPixels,
                rawWorkspaceAnalyzedStats,
                rawWorkspaceAnalyzedMs);
            const RawDevelopmentGraphScopeReadback localRangeScope =
                rawWorkspacePipeline->GetRawDevelopmentGraphScopeReadback();

            requestedGraphScopeStage =
                RawDevelopmentGraphScopeStage::FinishToneInput;
            std::vector<unsigned char> finishToneScopePixels;
            GraphExecutionStats finishToneScopeGraphStats;
            double finishToneScopeMs = 0.0;
            const bool finishToneScopeRenderOk = runRawWorkspaceGraph(
                rawWorkspaceRecipe,
                true,
                0,
                requestBase + 123,
                finishToneScopePixels,
                nullptr,
                finishToneScopeGraphStats,
                finishToneScopeMs);
            const RawDevelopmentGraphScopeReadback finishToneScope =
                rawWorkspacePipeline->GetRawDevelopmentGraphScopeReadback();

            requestedGraphScopeStage =
                RawDevelopmentGraphScopeStage::ColorWarpInput;
            std::vector<unsigned char> colorWarpScopePixels;
            GraphExecutionStats colorWarpScopeGraphStats;
            double colorWarpScopeMs = 0.0;
            const bool colorWarpScopeRenderOk = runRawWorkspaceGraph(
                rawWorkspaceRecipe,
                true,
                0,
                requestBase + 128,
                colorWarpScopePixels,
                nullptr,
                colorWarpScopeGraphStats,
                colorWarpScopeMs);
            const RawDevelopmentGraphScopeReadback colorWarpScope =
                rawWorkspacePipeline->GetRawDevelopmentGraphScopeReadback();

            requestedGraphScopeStage =
                RawDevelopmentGraphScopeStage::LocalRangeInput;
            const bool rawWorkspaceInteractiveOk = runRawWorkspaceGraph(
                rawWorkspaceRecipe,
                false,
                0,
                requestBase + 102,
                rawWorkspaceInteractivePixels,
                &rawWorkspaceInteractiveFloatPixels,
                rawWorkspaceInteractiveStats,
                rawWorkspaceInteractiveMs);
            const RawDevelopmentGraphScopeReadback interactiveScope =
                rawWorkspacePipeline->GetRawDevelopmentGraphScopeReadback();
            requestedGraphScopeStage = RawDevelopmentGraphScopeStage::None;
            requestedGraphScopeMaxDimension = 0;

            const auto scopeReadbackValid = [](const RawDevelopmentGraphScopeReadback& scope,
                                                RawDevelopmentGraphScopeStage stage,
                                                bool expectControlSignal) {
                const std::size_t expectedPixels =
                    static_cast<std::size_t>(std::max(0, scope.width)) *
                    static_cast<std::size_t>(std::max(0, scope.height));
                const std::size_t expectedElements =
                    expectedPixels * 3u;
                const bool controlSignalValid = expectControlSignal
                    ? scope.controlSignalDomain == "edge-aware-scene-ev" &&
                        scope.controlSignal.size() == expectedPixels &&
                        std::all_of(
                            scope.controlSignal.begin(),
                            scope.controlSignal.end(),
                            [](float value) { return std::isfinite(value); })
                    : scope.controlSignalDomain.empty() &&
                        scope.controlSignal.empty();
                return scope.valid &&
                    scope.stage == stage &&
                    scope.sceneLinearBeforeViewTransform &&
                    scope.width > 0 && scope.height > 0 &&
                    scope.width <= 64 && scope.height <= 64 &&
                    scope.sourceWidth > 0 && scope.sourceHeight > 0 &&
                    scope.pixels.size() == expectedElements &&
                    std::all_of(
                        scope.pixels.begin(),
                        scope.pixels.end(),
                        [](float value) { return std::isfinite(value); }) &&
                    controlSignalValid;
            };
            const bool graphScopeContractOk =
                scopeReadbackValid(
                    localRangeScope,
                    RawDevelopmentGraphScopeStage::LocalRangeInput,
                    true) &&
                finishToneScopeRenderOk &&
                scopeReadbackValid(
                    finishToneScope,
                    RawDevelopmentGraphScopeStage::FinishToneInput,
                    false) &&
                colorWarpScopeRenderOk &&
                scopeReadbackValid(
                    colorWarpScope,
                    RawDevelopmentGraphScopeStage::ColorWarpInput,
                    false) &&
                !interactiveScope.valid &&
                interactiveScope.pixels.empty() &&
                interactiveScope.controlSignal.empty();
            success = success && graphScopeContractOk;
            std::cout
                << "RAW workspace graph scopes: "
                << path.filename().string()
                << " local=" << localRangeScope.width << "x" << localRangeScope.height
                << " guided=" <<
                    (localRangeScope.controlSignalDomain == "edge-aware-scene-ev")
                << " finish=" << finishToneScope.width << "x" << finishToneScope.height
                << " finishMs=" << finishToneScopeMs
                << " color=" << colorWarpScope.width << "x" << colorWarpScope.height
                << " colorMs=" << colorWarpScopeMs
                << " interactiveSkipped=" << !interactiveScope.valid
                << " passed=" << graphScopeContractOk
                << "\n";
            if (!graphScopeContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: the Zones/Curve/Color "
                    << "scope readback was missing, used the wrong scene-linear "
                    << "boundary/control signal, exceeded its decimation bound, or "
                    << "ran during the interactive analysis-free pass for "
                    << path.string()
                    << "\n";
            }

            Stack::RawRecipe::RawDevelopmentRecipe activeColorWarpRecipe =
                rawWorkspaceRecipe;
            activeColorWarpRecipe.colorWarp.enabled = true;
            Stack::RawRecipe::RawColorWarpPin smokeColorPin;
            smokeColorPin.id = "real-raw-smoke-color";
            smokeColorPin.name = "Real RAW smoke color";
            smokeColorPin.sourceA = 0.0f;
            smokeColorPin.sourceB = 0.0f;
            smokeColorPin.targetA = 0.12f;
            smokeColorPin.targetB = 0.08f;
            smokeColorPin.radius = 0.75f;
            smokeColorPin.softness = 0.6f;
            smokeColorPin.evCurve =
                Stack::RawRecipe::MakeColorWarpEvCurveHump(
                    -1.0f, 5.0f, 0.75f);
            smokeColorPin.regionId = "real-raw-smoke-region";
            smokeColorPin.lightnessDeltaEv = 0.15f;
            activeColorWarpRecipe.colorWarp.pins = { smokeColorPin };
            Stack::RawRecipe::RawColorWarpRegion smokeColorRegion;
            smokeColorRegion.id = smokeColorPin.regionId;
            smokeColorRegion.spatialMode =
                Stack::RawRecipe::RawColorWarpSpatialMode::Cohesive;
            smokeColorRegion.featherPixels = 12.0f;
            smokeColorRegion.featherDirection =
                Stack::RawRecipe::RawColorWarpFeatherDirection::Centered;
            smokeColorRegion.circles.push_back({
                "real-raw-smoke-circle", 0.5f, 0.5f, 0.18f, 0.18f,
                Stack::RawRecipe::RawColorWarpInterpretationMode::GuidedFamily,
                Stack::RawRecipe::RawColorWarpSamplePolarity::Include
            });
            activeColorWarpRecipe.colorWarp.regions = { smokeColorRegion };
            std::vector<unsigned char> colorWarpPixels;
            std::vector<unsigned char> repeatedColorWarpPixels;
            GraphExecutionStats colorWarpStats;
            GraphExecutionStats repeatedColorWarpStats;
            double colorWarpMs = 0.0;
            double repeatedColorWarpMs = 0.0;
            std::vector<unsigned char> targetDraggedColorWarpPixels;
            GraphExecutionStats targetDraggedColorWarpStats;
            double targetDraggedColorWarpMs = 0.0;
            const bool colorWarpRenderOk = runRawWorkspaceGraph(
                activeColorWarpRecipe,
                false,
                0,
                requestBase + 129,
                colorWarpPixels,
                nullptr,
                colorWarpStats,
                colorWarpMs);
            const bool repeatedColorWarpRenderOk = runRawWorkspaceGraph(
                activeColorWarpRecipe,
                false,
                0,
                requestBase + 130,
                repeatedColorWarpPixels,
                nullptr,
                repeatedColorWarpStats,
                repeatedColorWarpMs);
            auto targetDraggedColorWarpRecipe = activeColorWarpRecipe;
            targetDraggedColorWarpRecipe.colorWarp.pins.front().targetA = 0.10f;
            const bool targetDraggedColorWarpRenderOk = runRawWorkspaceGraph(
                targetDraggedColorWarpRecipe,
                false,
                0,
                requestBase + 131,
                targetDraggedColorWarpPixels,
                nullptr,
                targetDraggedColorWarpStats,
                targetDraggedColorWarpMs);
            std::size_t changedColorWarpBytes = 0;
            if (colorWarpPixels.size() == rawWorkspaceInteractivePixels.size()) {
                for (std::size_t byte = 0; byte < colorWarpPixels.size(); ++byte) {
                    changedColorWarpBytes +=
                        colorWarpPixels[byte] != rawWorkspaceInteractivePixels[byte]
                        ? 1u
                        : 0u;
                }
            }
            const bool colorWarpGpuContractOk =
                colorWarpRenderOk &&
                repeatedColorWarpRenderOk &&
                targetDraggedColorWarpRenderOk &&
                !colorWarpPixels.empty() &&
                changedColorWarpBytes > 0u &&
                repeatedColorWarpPixels == colorWarpPixels &&
                targetDraggedColorWarpStats.colorWarpMaskCacheHits > 0 &&
                targetDraggedColorWarpStats.colorWarpMaskCacheMisses == 0;
            success = success && colorWarpGpuContractOk;
            std::cout
                << "RAW workspace Color Warp: "
                << path.filename().string()
                << " changedBytes=" << changedColorWarpBytes
                << " firstMs=" << colorWarpMs
                << " repeatMs=" << repeatedColorWarpMs
                << " targetDragMs=" << targetDraggedColorWarpMs
                << " maskHits=" << targetDraggedColorWarpStats.colorWarpMaskCacheHits
                << " maskBytes=" << targetDraggedColorWarpStats.colorWarpMaskBytesRetained
                << " passed=" << colorWarpGpuContractOk
                << "\n";
            if (!colorWarpGpuContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: Color Warp "
                    << "did not render, change the selected family, or repeat "
                    << "deterministically for "
                    << path.string()
                    << "\n";
            }

            const auto gradingScopeReadbackValid = [](
                                                       const RawDevelopmentGradingScopeReadback& scope,
                                                       RawDevelopmentGradingScopeSource expectedSource) {
                const std::size_t expectedPixels =
                    static_cast<std::size_t>(std::max(0, scope.width)) *
                    static_cast<std::size_t>(std::max(0, scope.height));
                return scope.valid &&
                    scope.source == expectedSource &&
                    scope.width > 0 && scope.height > 0 &&
                    scope.width <= 64 && scope.height <= 64 &&
                    scope.sourceWidth > 0 && scope.sourceHeight > 0 &&
                    scope.pixels.size() == expectedPixels * 3u &&
                    std::all_of(
                        scope.pixels.begin(),
                        scope.pixels.end(),
                        [](float value) { return std::isfinite(value); });
            };
            auto captureGradingScope = [&] (
                                           const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
                                           RawDevelopmentGradingScopeSource source,
                                           std::uint64_t revision,
                                           RawDevelopmentGradingScopeReadback& outScope) {
                requestedGradingScopeSource = source;
                requestedGradingScopeMaxDimension = 64;
                std::vector<unsigned char> outputPixels;
                GraphExecutionStats graphStats;
                double elapsedMilliseconds = 0.0;
                const bool renderOk = runRawWorkspaceGraph(
                    recipe,
                    false,
                    0,
                    revision,
                    outputPixels,
                    nullptr,
                    graphStats,
                    elapsedMilliseconds);
                const bool published =
                    rawWorkspacePipeline->PollRawDevelopmentGradingScopeReadback();
                outScope =
                    rawWorkspacePipeline->GetRawDevelopmentGradingScopeReadback();
                return renderOk && published &&
                    gradingScopeReadbackValid(outScope, source);
            };
            RawDevelopmentGradingScopeReadback neutralScopeBefore;
            RawDevelopmentGradingScopeReadback displayScopeBefore;
            RawDevelopmentGradingScopeReadback neutralScopeAfter;
            RawDevelopmentGradingScopeReadback displayScopeAfter;
            const bool neutralScopeBeforeOk = captureGradingScope(
                rawWorkspaceRecipe,
                RawDevelopmentGradingScopeSource::NeutralScene,
                requestBase + 124,
                neutralScopeBefore);
            const bool displayScopeBeforeOk = captureGradingScope(
                rawWorkspaceRecipe,
                RawDevelopmentGradingScopeSource::DisplayCandidate,
                requestBase + 125,
                displayScopeBefore);
            Stack::RawRecipe::RawDevelopmentRecipe gradingAdjustedRecipe =
                rawWorkspaceRecipe;
            gradingAdjustedRecipe.viewTransform.layerJson["exposure"] = 0.75f;
            const bool neutralScopeAfterOk = captureGradingScope(
                gradingAdjustedRecipe,
                RawDevelopmentGradingScopeSource::NeutralScene,
                requestBase + 126,
                neutralScopeAfter);
            const bool displayScopeAfterOk = captureGradingScope(
                gradingAdjustedRecipe,
                RawDevelopmentGradingScopeSource::DisplayCandidate,
                requestBase + 127,
                displayScopeAfter);
            const auto checkChangedScopeRequest = [&](int edge, bool includePixels) {
                requestedGradingScopeMaxDimension = edge;
                requestedGradingScopePixels = includePixels;
                std::vector<unsigned char> pixels;
                GraphExecutionStats stats;
                double milliseconds = 0;
                const bool rendered = runRawWorkspaceGraph(gradingAdjustedRecipe, false, 0,
                    requestBase + 127, pixels, nullptr, stats, milliseconds);
                const bool published = rawWorkspacePipeline->PollRawDevelopmentGradingScopeReadback();
                const auto& scope = rawWorkspacePipeline->GetRawDevelopmentGradingScopeReadback();
                const auto count = static_cast<std::size_t>(scope.width) * scope.height * 3;
                const bool valid = rendered && published && scope.valid && scope.visualization &&
                    scope.generation == requestBase + 127 &&
                    scope.source == RawDevelopmentGradingScopeSource::DisplayCandidate &&
                    std::max(scope.width, scope.height) == std::min(edge, std::max(scope.sourceWidth, scope.sourceHeight)) &&
                    (includePixels ? scope.pixels.size() == count : scope.pixels.empty());
                if (!valid) std::cerr << "Grading request change failed: edge=" << edge << " pixels=" << includePixels
                    << " rendered=" << rendered << " published=" << published << " valid=" << scope.valid
                    << " plot=" << static_cast<bool>(scope.visualization) << " size=" << scope.width << 'x' << scope.height
                    << " samples=" << scope.pixels.size() << " generation=" << scope.generation << '\n';
                return valid;
            };
            // Request options can change while the rendered image generation stays cached.
            const bool plotRequestChangesOk = checkChangedScopeRequest(64, false) &&
                checkChangedScopeRequest(48, false) && checkChangedScopeRequest(48, true);
            requestedGradingScopePixels = true;
            requestedGradingScopeSource =
                RawDevelopmentGradingScopeSource::None;
            requestedGradingScopeMaxDimension = 0;

            const auto maximumScopeDifference = [](
                                                  const RawDevelopmentGradingScopeReadback& first,
                                                  const RawDevelopmentGradingScopeReadback& second) {
                if (first.width != second.width ||
                    first.height != second.height ||
                    first.pixels.size() != second.pixels.size()) {
                    return std::numeric_limits<float>::infinity();
                }
                float maximum = 0.0f;
                for (std::size_t index = 0; index < first.pixels.size(); ++index) {
                    maximum = std::max(
                        maximum,
                        std::abs(first.pixels[index] - second.pixels[index]));
                }
                return maximum;
            };
            const float neutralScopeDelta = maximumScopeDifference(
                neutralScopeBefore,
                neutralScopeAfter);
            const float displayScopeDelta = maximumScopeDifference(
                displayScopeBefore,
                displayScopeAfter);
            const bool gradingScopeContractOk =
                plotRequestChangesOk &&
                neutralScopeBeforeOk &&
                displayScopeBeforeOk &&
                neutralScopeAfterOk &&
                displayScopeAfterOk &&
                neutralScopeBefore.sceneLinear &&
                !neutralScopeBefore.encodedSrgb &&
                displayScopeBefore.encodedSrgb &&
                neutralScopeDelta <= 0.000001f &&
                std::isfinite(displayScopeDelta) &&
                displayScopeDelta > 0.0001f;
            success = success && gradingScopeContractOk;
            std::cout
                << "RAW workspace live grading scopes: "
                << path.filename().string()
                << " neutral=" << neutralScopeBefore.width << "x"
                << neutralScopeBefore.height
                << " display=" << displayScopeBefore.width << "x"
                << displayScopeBefore.height
                << " neutralDelta=" << neutralScopeDelta
                << " displayDelta=" << displayScopeDelta
                << " passed=" << gradingScopeContractOk
                << "\n";
            if (!gradingScopeContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: the asynchronous "
                    << "grading scope did not preserve the Neutral Scene boundary, "
                    << "did not react at the Display Candidate boundary, or exceeded "
                    << "its decimation contract for "
                    << path.string()
                    << "\n";
            }

            Stack::RawRecipe::RawDevelopmentRecipe curveBaselineRecipe =
                rawWorkspaceRecipe;
            curveBaselineRecipe.viewTransform.layerJson["enabled"] = false;
            curveBaselineRecipe.finishTone.layerJson["domain"] = 1;
            std::vector<unsigned char> curveBaselinePixels;
            std::vector<float> curveBaselineFloatPixels;
            GraphExecutionStats curveBaselineStats;
            double curveBaselineMs = 0.0;
            const bool curveBaselineOk = runRawWorkspaceGraph(
                curveBaselineRecipe,
                false,
                0,
                requestBase + 124,
                curveBaselinePixels,
                &curveBaselineFloatPixels,
                curveBaselineStats,
                curveBaselineMs);
            std::array<float, 3> curveSelectedMaxDiff { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> curveOtherMaxDiff { 0.0f, 0.0f, 0.0f };
            std::array<int, 3> curveCacheHits { 0, 0, 0 };
            bool independentCurveContractOk =
                curveBaselineOk &&
                !curveBaselineFloatPixels.empty() &&
                std::all_of(
                    curveBaselineFloatPixels.begin(),
                    curveBaselineFloatPixels.end(),
                    [](float value) { return std::isfinite(value); });
            for (int channel = 0; channel < 3; ++channel) {
                Stack::RawRecipe::RawDevelopmentRecipe curveRecipe =
                    curveBaselineRecipe;
                Stack::RawRecipe::RawPointCurveComponent component;
                component.points = {
                    { 0.0f, 0.0f, 1 },
                    { 0.42f, 0.66f, 1 },
                    { 1.0f, 1.0f, 1 }
                };
                Stack::RawRecipe::StorePointCurveComponentInFinishToneJson(
                    curveRecipe.finishTone.layerJson,
                    static_cast<Stack::RawRecipe::RawPointCurveChannel>(channel + 1),
                    component);
                std::vector<unsigned char> curvePixels;
                std::vector<float> curveFloatPixels;
                GraphExecutionStats curveStats;
                double curveMs = 0.0;
                const bool curveRenderOk = runRawWorkspaceGraph(
                    curveRecipe,
                    false,
                    0,
                    requestBase + 125 + static_cast<std::uint64_t>(channel),
                    curvePixels,
                    &curveFloatPixels,
                    curveStats,
                    curveMs);
                curveCacheHits[static_cast<std::size_t>(channel)] =
                    curveStats.rawStageCacheHits;
                bool curveFinite = curveFloatPixels.size() ==
                    curveBaselineFloatPixels.size();
                if (curveFinite) {
                    for (std::size_t pixel = 0;
                         pixel + 3u < curveFloatPixels.size();
                         pixel += 4u) {
                        for (int rgbChannel = 0; rgbChannel < 3; ++rgbChannel) {
                            const float value = curveFloatPixels[
                                pixel + static_cast<std::size_t>(rgbChannel)];
                            const float baseline = curveBaselineFloatPixels[
                                pixel + static_cast<std::size_t>(rgbChannel)];
                            if (!std::isfinite(value)) {
                                curveFinite = false;
                                break;
                            }
                            const float difference = std::abs(value - baseline);
                            if (rgbChannel == channel) {
                                curveSelectedMaxDiff[static_cast<std::size_t>(channel)] =
                                    std::max(
                                        curveSelectedMaxDiff[static_cast<std::size_t>(channel)],
                                        difference);
                            } else {
                                curveOtherMaxDiff[static_cast<std::size_t>(channel)] =
                                    std::max(
                                        curveOtherMaxDiff[static_cast<std::size_t>(channel)],
                                        difference);
                            }
                        }
                        if (!curveFinite) {
                            break;
                        }
                    }
                }
                independentCurveContractOk =
                    independentCurveContractOk &&
                    curveRenderOk &&
                    curveFinite &&
                    !curvePixels.empty() &&
                    curveSelectedMaxDiff[static_cast<std::size_t>(channel)] > 0.0002f &&
                    curveOtherMaxDiff[static_cast<std::size_t>(channel)] <= 0.0002f &&
                    curveStats.rawStageCacheHits > 0;
            }
            success = success && independentCurveContractOk;
            std::cout
                << "RAW workspace point curves: "
                << path.filename().string()
                << " baselineMs=" << curveBaselineMs
                << " selectedDiff="
                << curveSelectedMaxDiff[0] << ","
                << curveSelectedMaxDiff[1] << ","
                << curveSelectedMaxDiff[2]
                << " otherDiff="
                << curveOtherMaxDiff[0] << ","
                << curveOtherMaxDiff[1] << ","
                << curveOtherMaxDiff[2]
                << " cacheHits="
                << curveCacheHits[0] << ","
                << curveCacheHits[1] << ","
                << curveCacheHits[2]
                << " passed=" << independentCurveContractOk
                << "\n";
            if (!independentCurveContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: the composed "
                    << "4096-sample point-curve LUT was blank/nonfinite, coupled "
                    << "an unedited RGB channel, or failed to reuse an upstream "
                    << "RAW stage for "
                    << path.string()
                    << "\n";
            }

            // Reproduce the Editor export failure reported for a compact RAW
            // Development graph followed by an ordinary graph adjustment.
            // The live pipeline deliberately retains a small source buffer
            // while its canvas is resized to the RAW output dimensions. The
            // export path must not pair that stale byte count with the larger
            // dimensions when it builds the full-resolution render.
            bool rawEditorExportContractOk = false;
            {
                EditorModule exportEditor;
                RenderPipeline& livePipeline = exportEditor.GetPipeline();
                livePipeline.Initialize();
                constexpr int staleSourceWidth = 320;
                constexpr int staleSourceHeight = 339;
                std::vector<unsigned char> staleSourcePixels(
                    static_cast<std::size_t>(staleSourceWidth) *
                        static_cast<std::size_t>(staleSourceHeight) * 4u,
                    127u);
                livePipeline.LoadSourceFromPixels(
                    staleSourcePixels.data(),
                    staleSourceWidth,
                    staleSourceHeight,
                    4);
                livePipeline.Resize(
                    Raw::DisplayWidth(raw.metadata),
                    Raw::DisplayHeight(raw.metadata));

                EditorNodeGraph::Graph& exportGraph =
                    exportEditor.GetNodeGraph();
                exportGraph.Clear();
                EditorNodeGraph::RawDevelopmentPayload compactPayload;
                compactPayload.recipe = rawWorkspaceRecipe;
                const int compactId = exportGraph.AddRawDevelopmentNode(
                    std::move(compactPayload), { 0.0f, 0.0f })->id;
                EditorNodeGraph::Node* exposureNode =
                    exportGraph.AddTechnicalImageNode(
                        Stack::NodeMath::TechnicalImageOperation::Exposure,
                        { 240.0f, 0.0f });
                exposureNode->technicalImageSettings.exposureValue = 0.10f;
                const int exposureId = exposureNode->id;
                const int outputId = exportGraph.AddOutputNode(
                    { 480.0f, 0.0f }, true)->id;
                const bool authored =
                    exportGraph.TryConnectSockets(
                        compactId,
                        EditorNodeGraph::kImageOutputSocketId,
                        exposureId,
                        EditorNodeGraph::kImageInputSocketId) &&
                    exportGraph.TryConnectSockets(
                        exposureId,
                        EditorNodeGraph::kImageOutputSocketId,
                        outputId,
                        EditorNodeGraph::kImageInputSocketId);
                std::vector<unsigned char> exportPixels;
                int exportWidth = 0;
                int exportHeight = 0;
                rawEditorExportContractOk =
                    authored &&
                    exportEditor.BuildSingleOutputExportRaster(
                        exportPixels,
                        exportWidth,
                        exportHeight) &&
                    exportWidth > 0 &&
                    exportHeight > 0 &&
                    !exportPixels.empty() &&
                    std::any_of(
                        exportPixels.begin(),
                        exportPixels.end(),
                        [](unsigned char value) { return value != 0; });
            }
            success = success && rawEditorExportContractOk;
            std::cout
                << "RAW workspace Editor export: "
                << path.filename().string()
                << " staleSourceBytes=" << (320 * 339 * 4)
                << " fullCanvas="
                << Raw::DisplayWidth(raw.metadata) << "x"
                << Raw::DisplayHeight(raw.metadata)
                << " passed=" << rawEditorExportContractOk
                << "\n";
            if (!rawEditorExportContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: Editor export "
                    << "could not safely render a compact RAW Development graph "
                    << "with a downstream adjustment after rejecting a stale "
                    << "short source buffer for " << path.string() << "\n";
            }

            Stack::RawRecipe::RawDevelopmentRecipe downstreamRecipe = rawWorkspaceRecipe;
            downstreamRecipe.viewTransform.layerJson["exposure"] =
                downstreamRecipe.viewTransform.layerJson.value("exposure", 0.0f) + 0.20f;
            const bool rawWorkspaceDownstreamOk = runRawWorkspaceGraph(
                downstreamRecipe,
                false,
                0,
                requestBase + 103,
                rawWorkspaceDownstreamPixels,
                nullptr,
                rawWorkspaceDownstreamStats,
                rawWorkspaceDownstreamMs);

            Stack::RawRecipe::RawDevelopmentRecipe upstreamRecipe = rawWorkspaceRecipe;
            upstreamRecipe.preToneExposureEv += 0.20f;
            const bool rawWorkspaceUpstreamOk = runRawWorkspaceGraph(
                upstreamRecipe,
                false,
                0,
                requestBase + 104,
                rawWorkspaceUpstreamPixels,
                nullptr,
                rawWorkspaceUpstreamStats,
                rawWorkspaceUpstreamMs);

            Stack::RawRecipe::RawDevelopmentRecipe rgbDenoiseRecipe =
                rawWorkspaceRecipe;
            rgbDenoiseRecipe.rgbDenoise.enabled = true;
            rgbDenoiseRecipe.rgbDenoise.colorNoise = 0.35f;
            rgbDenoiseRecipe.rgbDenoise.luminanceNoise = 0.20f;
            rgbDenoiseRecipe.rgbDenoise.detailProtection = 0.75f;
            rgbDenoiseRecipe.rgbDenoise.lumaMap.baseMultiplier = 0.20f;
            rgbDenoiseRecipe.rgbDenoise.chromaMap.baseMultiplier = 0.35f;
            std::vector<unsigned char> rgbDenoisePixels;
            std::vector<unsigned char> rgbDenoiseExposurePixels;
            std::vector<unsigned char> rgbDenoiseRepeatPixels;
            std::vector<float> rgbDenoiseFloatPixels;
            GraphExecutionStats rgbDenoiseStats;
            GraphExecutionStats rgbDenoiseExposureStats;
            GraphExecutionStats rgbDenoiseRepeatStats;
            double rgbDenoiseMs = 0.0;
            double rgbDenoiseExposureMs = 0.0;
            double rgbDenoiseRepeatMs = 0.0;
            const bool rgbDenoiseOk = runRawWorkspaceGraph(
                rgbDenoiseRecipe,
                false,
                0,
                requestBase + 120,
                rgbDenoisePixels,
                &rgbDenoiseFloatPixels,
                rgbDenoiseStats,
                rgbDenoiseMs);
            Stack::RawRecipe::RawDevelopmentRecipe rgbDenoiseExposureRecipe =
                rgbDenoiseRecipe;
            rgbDenoiseExposureRecipe.preToneExposureEv += 0.20f;
            const bool rgbDenoiseExposureOk = runRawWorkspaceGraph(
                rgbDenoiseExposureRecipe,
                false,
                0,
                requestBase + 121,
                rgbDenoiseExposurePixels,
                nullptr,
                rgbDenoiseExposureStats,
                rgbDenoiseExposureMs);
            const bool rgbDenoiseRepeatOk = runRawWorkspaceGraph(
                rgbDenoiseRecipe,
                false,
                0,
                requestBase + 122,
                rgbDenoiseRepeatPixels,
                nullptr,
                rgbDenoiseRepeatStats,
                rgbDenoiseRepeatMs);
            const bool rgbDenoiseFinite = std::all_of(
                rgbDenoiseFloatPixels.begin(),
                rgbDenoiseFloatPixels.end(),
                [](float value) { return std::isfinite(value); });
            const bool rgbDenoiseVisible =
                rgbDenoisePixels != rawWorkspaceInteractivePixels;
            const bool rgbDenoiseExposureVisible =
                rgbDenoiseExposurePixels != rgbDenoisePixels;
            const bool rgbDenoiseDeterministic =
                rgbDenoiseRepeatPixels == rgbDenoisePixels;
            const bool rgbDenoiseCacheContract =
                rgbDenoiseExposureStats.rawStageCacheHits > 0 &&
                rgbDenoiseRepeatStats.rawStageCacheHits > 0;
            const bool rgbDenoiseContractOk =
                rgbDenoiseOk &&
                rgbDenoiseExposureOk &&
                rgbDenoiseRepeatOk &&
                !rgbDenoiseFloatPixels.empty() &&
                rgbDenoiseFinite &&
                rgbDenoiseVisible &&
                rgbDenoiseExposureVisible &&
                rgbDenoiseDeterministic &&
                rgbDenoiseCacheContract;
            success = success && rgbDenoiseContractOk;
            std::cout
                << "RAW workspace RGB denoise: "
                << path.filename().string()
                << " enabledMs=" << rgbDenoiseMs
                << " exposureMs=" << rgbDenoiseExposureMs
                << " repeatMs=" << rgbDenoiseRepeatMs
                << " changed=" << rgbDenoiseVisible
                << " exposureChanged=" << rgbDenoiseExposureVisible
                << " deterministic=" << rgbDenoiseDeterministic
                << " exposureCacheHits="
                << rgbDenoiseExposureStats.rawStageCacheHits
                << " repeatCacheHits="
                << rgbDenoiseRepeatStats.rawStageCacheHits
                << " passed=" << rgbDenoiseContractOk
                << "\n";
            if (!rgbDenoiseContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: the "
                    << "post-demosaic RGB denoise stage was blank, inert, "
                    << "non-deterministic, or did not preserve its upstream "
                    << "cache across Exposure edits for "
                    << path.string()
                    << "\n";
            }

            Stack::RawRecipe::RawDevelopmentRecipe targetEditRecipe =
                rawWorkspaceRecipe;
            targetEditRecipe.localRange.targetZones.front().deltaEv = 1.0f;
            const bool rawWorkspaceTargetEditOk = runRawWorkspaceGraph(
                targetEditRecipe,
                false,
                0,
                requestBase + 105,
                rawWorkspaceTargetEditPixels,
                nullptr,
                rawWorkspaceTargetEditStats,
                rawWorkspaceTargetEditMs);
            std::size_t rawWorkspaceTargetEditChangedBytes = 0;
            if (rawWorkspaceTargetEditPixels.size() ==
                rawWorkspaceInteractivePixels.size()) {
                for (std::size_t byteIndex = 0;
                     byteIndex < rawWorkspaceTargetEditPixels.size();
                     ++byteIndex) {
                    rawWorkspaceTargetEditChangedBytes +=
                        rawWorkspaceTargetEditPixels[byteIndex] !=
                                rawWorkspaceInteractivePixels[byteIndex]
                            ? 1u
                            : 0u;
                }
            }
            const bool rawWorkspaceTargetEditVisible =
                rawWorkspaceTargetEditOk &&
                rawWorkspaceTargetEditChangedBytes > 0;
            success = success && rawWorkspaceTargetEditVisible;
            std::cout
                << "RAW workspace target edit publication: "
                << path.filename().string()
                << " changedBytes=" << rawWorkspaceTargetEditChangedBytes
                << " renderMs=" << rawWorkspaceTargetEditMs
                << " passed=" << rawWorkspaceTargetEditVisible
                << "\n";
            if (!rawWorkspaceTargetEditVisible) {
                std::cerr
                    << "Develop real RAW smoke validation failed: changing "
                    << "only an authored target-zone EV did not change the "
                    << "rendered photograph for "
                    << path.string()
                    << "\n";
            }

            constexpr float kFirstTargetU = 0.23f;
            constexpr float kFirstTargetV = 0.31f;
            constexpr float kSecondTargetU = 0.77f;
            constexpr float kSecondTargetV = 0.69f;
            const RawWorkspaceTargetSampleProbe firstTargetProbe =
                runRawWorkspaceTargetSample(
                    kFirstTargetU,
                    kFirstTargetV,
                    requestBase + 106);
            const RawWorkspaceTargetSampleProbe secondTargetProbe =
                runRawWorkspaceTargetSample(
                    kSecondTargetU,
                    kSecondTargetV,
                    requestBase + 107);
            const RawWorkspaceTargetSampleProbe repeatedFirstTargetProbe =
                runRawWorkspaceTargetSample(
                    kFirstTargetU,
                    kFirstTargetV,
                    requestBase + 108);
            const RawWorkspaceTargetSampleProbe authoredZoneTargetProbe =
                runRawWorkspaceTargetSample(
                    0.5f,
                    0.5f,
                    requestBase + 109);

            const auto targetProbeIsCurrent =
                [](const RawWorkspaceTargetSampleProbe& probe,
                    float expectedU,
                    float expectedV) {
                    return probe.renderValid &&
                        probe.sampleValid &&
                        probe.overlayTextureValid &&
                        probe.publishedTextureValid &&
                        std::isfinite(probe.sceneEv) &&
                        std::isfinite(probe.sceneLuma) &&
                        std::all_of(
                            probe.sceneRgb.begin(),
                            probe.sceneRgb.end(),
                            [](float value) { return std::isfinite(value); }) &&
                        std::abs(probe.sampleU - expectedU) < 0.000001f &&
                        std::abs(probe.sampleV - expectedV) < 0.000001f;
                };
            const bool rawWorkspaceTargetSamplesCurrent =
                targetProbeIsCurrent(
                    firstTargetProbe,
                    kFirstTargetU,
                    kFirstTargetV) &&
                targetProbeIsCurrent(
                    secondTargetProbe,
                    kSecondTargetU,
                    kSecondTargetV) &&
                targetProbeIsCurrent(
                    repeatedFirstTargetProbe,
                    kFirstTargetU,
                    kFirstTargetV);
            const bool rawWorkspaceTargetOutputStable =
                firstTargetProbe.outputPixels == rawWorkspaceInteractivePixels &&
                secondTargetProbe.outputPixels == rawWorkspaceInteractivePixels &&
                repeatedFirstTargetProbe.outputPixels == rawWorkspaceInteractivePixels &&
                authoredZoneTargetProbe.outputPixels == rawWorkspaceInteractivePixels;
            const bool rawWorkspaceAuthoredZoneHitCurrent =
                targetProbeIsCurrent(authoredZoneTargetProbe, 0.5f, 0.5f) &&
                (authoredZoneTargetProbe.authoredZoneHitBits & 1u) != 0u &&
                authoredZoneTargetProbe.strongestAuthoredZoneWeight > 0.0f;
            const bool rawWorkspaceTargetHoverContractOk =
                rawWorkspaceTargetSamplesCurrent &&
                rawWorkspaceTargetOutputStable &&
                rawWorkspaceAuthoredZoneHitCurrent;
            success = success && rawWorkspaceTargetHoverContractOk;
            std::cout
                << "RAW workspace target hover: "
                << path.filename().string()
                << " firstValid=" << firstTargetProbe.sampleValid
                << " firstUv=(" << firstTargetProbe.sampleU
                << "," << firstTargetProbe.sampleV << ")"
                << " secondValid=" << secondTargetProbe.sampleValid
                << " secondUv=(" << secondTargetProbe.sampleU
                << "," << secondTargetProbe.sampleV << ")"
                << " repeatedFirstValid=" << repeatedFirstTargetProbe.sampleValid
                << " repeatedFirstUv=(" << repeatedFirstTargetProbe.sampleU
                << "," << repeatedFirstTargetProbe.sampleV << ")"
                << " authoredHitBits=0x" << std::hex
                << authoredZoneTargetProbe.authoredZoneHitBits << std::dec
                << " authoredHitWeight="
                << authoredZoneTargetProbe.strongestAuthoredZoneWeight
                << " overlaysValid="
                << (firstTargetProbe.overlayTextureValid &&
                    secondTargetProbe.overlayTextureValid &&
                    repeatedFirstTargetProbe.overlayTextureValid)
                << " publishedValid="
                << (firstTargetProbe.publishedTextureValid &&
                    secondTargetProbe.publishedTextureValid &&
                    repeatedFirstTargetProbe.publishedTextureValid)
                << " publishedMaxRgb=("
                << firstTargetProbe.publishedMaxRgb << ","
                << secondTargetProbe.publishedMaxRgb << ","
                << repeatedFirstTargetProbe.publishedMaxRgb << ")"
                << " outputStable=" << rawWorkspaceTargetOutputStable
                << " passed=" << rawWorkspaceTargetHoverContractOk
                << "\n";
            if (!rawWorkspaceTargetHoverContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: repeated Local "
                    << "Range target samples did not stay current, or their shared "
                    << "preview output changed for "
                    << path.string()
                    << "\n";
            }

            RenderGraphSnapshot targetOutlineGraph;
            targetOutlineGraph.outputNodeId = 2;
            targetOutlineGraph.rawWorkspaceLocalRangeOverlayMode =
                "target-outline";
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview.enabled =
                true;
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview
                .requestConnectedRefinement = true;
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview.provisional =
                false;
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview.generation =
                requestBase + 110;
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview.sourceU =
                kFirstTargetU;
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview.sourceV =
                kFirstTargetV;
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview
                .existingZoneIndex = -1;
            Stack::RawRecipe::RawLocalRangeTargetZone prospectiveOutlineZone =
                rawWorkspaceRecipe.localRange.targetZones.front();
            prospectiveOutlineZone.id = "__validation-target-outline__";
            prospectiveOutlineZone.centerEv = firstTargetProbe.sceneEv;
            prospectiveOutlineZone.coreHalfWidthEv = 0.35f;
            prospectiveOutlineZone.featherEv = 0.45f;
            prospectiveOutlineZone.deltaEv = 0.0f;
            prospectiveOutlineZone.scope =
                Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
            prospectiveOutlineZone.seeds = {
                { kFirstTargetU, kFirstTargetV }
            };
            targetOutlineGraph.rawWorkspaceLocalRangeTargetPreview
                .prospectiveZone = prospectiveOutlineZone;

            RenderGraphNode outlineRawDevelopmentNode;
            outlineRawDevelopmentNode.nodeId = 1;
            outlineRawDevelopmentNode.kind =
                RenderGraphNodeKind::RawDevelopment;
            outlineRawDevelopmentNode.requestRevision = requestBase + 110;
            outlineRawDevelopmentNode.rawDevelopment.recipe =
                rawWorkspaceRecipe;
            targetOutlineGraph.nodes.push_back(
                std::move(outlineRawDevelopmentNode));
            RenderGraphNode outlineOutputNode;
            outlineOutputNode.nodeId = 2;
            outlineOutputNode.kind = RenderGraphNodeKind::Output;
            outlineOutputNode.requestRevision = requestBase + 110;
            targetOutlineGraph.nodes.push_back(std::move(outlineOutputNode));
            targetOutlineGraph.links.push_back(
                RenderGraphLink{ 1, "imageOut", 2, "imageIn" });

            rawWorkspacePipeline->SetRawDevelopmentAnalysisEnabled(false);
            rawWorkspacePipeline->ExecuteGraph(targetOutlineGraph);
            glFinish();
            for (int poll = 0;
                 poll < 100 &&
                 !rawWorkspacePipeline->IsRawDevelopmentLocalRangeTargetPreviewRefined();
                 ++poll) {
                rawWorkspacePipeline->ExecuteGraph(targetOutlineGraph);
                glFinish();
                if (!rawWorkspacePipeline->IsRawDevelopmentLocalRangeTargetPreviewRefined()) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                }
            }
            int targetOutlineWidth = 0;
            int targetOutlineHeight = 0;
            const std::vector<unsigned char> targetOutlinePixels =
                rawWorkspacePipeline->GetRawDevelopmentLocalRangeOverlayPixels(
                    targetOutlineWidth,
                    targetOutlineHeight);
            std::size_t targetOutlineVisiblePixels = 0;
            std::size_t targetOutlineTransparentPixels = 0;
            std::size_t targetOutlineNeighboringVisiblePixels = 0;
            for (std::size_t pixel = 3;
                 pixel < targetOutlinePixels.size();
                 pixel += 4) {
                if (targetOutlinePixels[pixel] == 0u) {
                    ++targetOutlineTransparentPixels;
                } else {
                    ++targetOutlineVisiblePixels;
                }
            }
            if (targetOutlineWidth > 0 && targetOutlineHeight > 0) {
                const auto outlineVisibleAt = [&](int x, int y) {
                    if (x < 0 || x >= targetOutlineWidth ||
                        y < 0 || y >= targetOutlineHeight) {
                        return false;
                    }
                    const std::size_t alphaOffset =
                        (static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(targetOutlineWidth) +
                            static_cast<std::size_t>(x)) *
                            4u +
                        3u;
                    return alphaOffset < targetOutlinePixels.size() &&
                        targetOutlinePixels[alphaOffset] != 0u;
                };
                for (int y = 0; y < targetOutlineHeight; ++y) {
                    for (int x = 0; x < targetOutlineWidth; ++x) {
                        if (!outlineVisibleAt(x, y)) {
                            continue;
                        }
                        bool hasVisibleNeighbor = false;
                        for (int dy = -1;
                             dy <= 1 && !hasVisibleNeighbor;
                             ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if ((dx != 0 || dy != 0) &&
                                    outlineVisibleAt(x + dx, y + dy)) {
                                    hasVisibleNeighbor = true;
                                    break;
                                }
                            }
                        }
                        targetOutlineNeighboringVisiblePixels +=
                            hasVisibleNeighbor ? 1u : 0u;
                    }
                }
            }
            const std::size_t targetOutlinePixelCount =
                targetOutlineVisiblePixels + targetOutlineTransparentPixels;
            const RawLocalRangeTargetPreviewMetrics targetOutlineMetrics =
                rawWorkspacePipeline->GetRawDevelopmentLocalRangeTargetPreviewMetrics();
            const bool targetOutlineContractOk =
                rawWorkspacePipeline->IsRawDevelopmentLocalRangeTargetPreviewRefined() &&
                targetOutlineWidth > 0 &&
                targetOutlineHeight > 0 &&
                targetOutlineVisiblePixels > 0 &&
                targetOutlinePixelCount > 0 &&
                targetOutlineTransparentPixels * 100 >
                    targetOutlinePixelCount * 94 &&
                targetOutlineNeighboringVisiblePixels * 10 >=
                    targetOutlineVisiblePixels * 8 &&
                targetOutlineMetrics.maximumDimension > 0 &&
                targetOutlineMetrics.maximumDimension <= 768;
            success = success && targetOutlineContractOk;
            std::cout
                << "RAW workspace target outline: "
                << path.filename().string()
                << " refined="
                << rawWorkspacePipeline->IsRawDevelopmentLocalRangeTargetPreviewRefined()
                << " visiblePixels=" << targetOutlineVisiblePixels
                << " neighboringVisiblePixels="
                << targetOutlineNeighboringVisiblePixels
                << " transparentPixels=" << targetOutlineTransparentPixels
                << " maxDimension=" << targetOutlineMetrics.maximumDimension
                << " qualifierIssueMs="
                << targetOutlineMetrics.qualifierIssueMs
                << " readbackCopyMs="
                << targetOutlineMetrics.readbackCopyMs
                << " floodFillMs=" << targetOutlineMetrics.floodFillMs
                << " uploadMs=" << targetOutlineMetrics.uploadMs
                << " passed=" << targetOutlineContractOk
                << "\n";
            int discardedOutlineWidth = 0;
            int discardedOutlineHeight = 0;
            const unsigned int discardedOutlineTexture =
                rawWorkspacePipeline->TakeRawDevelopmentLocalRangeOverlayTexture(
                        discardedOutlineWidth,
                        discardedOutlineHeight);
            if (discardedOutlineTexture != 0) {
                glDeleteTextures(1, &discardedOutlineTexture);
            }
            if (!targetOutlineContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: target "
                    << "outline was not sparse, refined, or bounded to its "
                    << "768-pixel auxiliary mask for "
                    << path.string()
                    << "\n";
            }

            const bool interactivePixelsMatchAnalyzed =
                rawWorkspaceAnalyzedPixels == rawWorkspaceInteractivePixels &&
                rawWorkspaceAnalyzedFloatPixels == rawWorkspaceInteractiveFloatPixels;
            const bool rawPlacementCacheReused =
                rawWorkspaceInteractiveStats.rawStageCacheHits > 0 &&
                rawWorkspaceDownstreamStats.rawStageCacheHits > 0;
            const bool upstreamRawPlacementInvalidated =
                rawWorkspaceUpstreamStats.rawStageCacheMisses > 0;
            const bool rawWorkspacePerformanceContractOk =
                rawWorkspaceWarmOk &&
                rawWorkspaceAnalyzedOk &&
                rawWorkspaceInteractiveOk &&
                rawWorkspaceDownstreamOk &&
                rawWorkspaceUpstreamOk &&
                interactivePixelsMatchAnalyzed &&
                rawPlacementCacheReused &&
                upstreamRawPlacementInvalidated;
            success = success && rawWorkspacePerformanceContractOk;
            std::cout
                << "RAW workspace interactive performance: "
                << path.filename().string()
                << " warmMs=" << rawWorkspaceWarmMs
                << " analyzedMs=" << rawWorkspaceAnalyzedMs
                << " interactiveMs=" << rawWorkspaceInteractiveMs
                << " downstreamMs=" << rawWorkspaceDownstreamMs
                << " upstreamMs=" << rawWorkspaceUpstreamMs
                << " analyzedCacheHits=" << rawWorkspaceAnalyzedStats.rawStageCacheHits
                << " interactiveCacheHits=" << rawWorkspaceInteractiveStats.rawStageCacheHits
                << " downstreamCacheHits=" << rawWorkspaceDownstreamStats.rawStageCacheHits
                << " upstreamCacheMisses=" << rawWorkspaceUpstreamStats.rawStageCacheMisses
                << " pixelsIdentical=" << interactivePixelsMatchAnalyzed
                << " passed=" << rawWorkspacePerformanceContractOk
                << "\n";
            if (!rawWorkspacePerformanceContractOk) {
                std::cerr
                    << "Develop real RAW smoke validation failed: the RAW workspace "
                    << "interactive/settled output or stage-cache contract failed for "
                    << path.string()
                    << "\n";
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

namespace Stack::Validation {

bool ValidateDevelopNodeSmoke() {
    return ::ValidateDevelopNodeSmoke();
}

bool ValidateDevelopRealRawSmoke(int rawArgCount, char** rawArgs) {
    return ::ValidateDevelopRealRawSmoke(rawArgCount, rawArgs);
}

} // namespace Stack::Validation
