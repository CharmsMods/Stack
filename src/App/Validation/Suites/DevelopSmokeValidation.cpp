#include "App/Validation/ValidationSuites.h"

#include "App/Validation/ValidationImageUtils.h"
#include "Editor/EditorModule.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Raw/RawLoader.h"
#include "Raw/RawGpuPipeline.h"
#include "Renderer/GLLoader.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
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
    bool manualOrientationNonBlank = false;
    bool developGraphBalancedNonBlank = false;
    bool developGraphDarkNonBlank = false;
    bool developGraphHighlightNonBlank = false;
    bool developGraphNoisyNonBlank = false;
    bool developGraphNoisyToneOnlyNonBlank = false;
    bool developGraphDngCalibrationNonBlank = false;
    bool manualRawDecodeChainNonBlank = false;
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
    float manualRawDecodeChainMaxRgb = 0.0f;
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

            graphPipeline.Resize(kRawWidth, kRawHeight);
            graphPipeline.ExecuteGraph(graph);
            outMaxRgb = ReadTextureMaxRgb(
                graphPipeline.GetOutputTexture(),
                graphPipeline.GetCanvasWidth(),
                graphPipeline.GetCanvasHeight());
            int outputW = 0;
            int outputH = 0;
            const std::vector<unsigned char> outputPixels = graphPipeline.GetOutputPixels(outputW, outputH);
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
            manualOrientationNonBlank &&
            developGraphBalancedNonBlank &&
            developGraphDarkNonBlank &&
            developGraphHighlightNonBlank &&
            developGraphDngCalibrationNonBlank &&
            manualRawDecodeChainNonBlank &&
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
            << " manualRawDecodeChainNonBlank=" << manualRawDecodeChainNonBlank
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

namespace Stack::Validation {

bool ValidateDevelopNodeSmoke() {
    return ::ValidateDevelopNodeSmoke();
}

bool ValidateDevelopRealRawSmoke(int rawArgCount, char** rawArgs) {
    return ::ValidateDevelopRealRawSmoke(rawArgCount, rawArgs);
}

} // namespace Stack::Validation
