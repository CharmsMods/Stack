#include "App/Validation/ValidationSuites.h"

#include "App/Validation/ValidationImageUtils.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

namespace {

using Stack::Validation::ComputeAverageNormalizedLuma;
using Stack::Validation::CountPixelsWithNonZeroAlpha;
using Stack::Validation::CountPixelsWithNonZeroRgb;
using Stack::Validation::ReadTextureMaxRgb;

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
} // namespace

namespace Stack::Validation {

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
            imageNode.image.pixels = MakeSharedPixelBufferCopy(pixels);
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

} // namespace Stack::Validation
