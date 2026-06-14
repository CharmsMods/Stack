#include "EditorModule.h"

#include "Async/TaskSystem.h"
#include "Layers/ToneLayers.h"
#include "NodeGraph/EditorNodeGraphDefinitions.h"
#include "NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Raw/RawLoader.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "ThirdParty/stb_image.h"
#include "App/Resources/EmbeddedTabIcons.h"
#include "ThirdParty/stb_image_write.h"
#include "App/settings/AppearanceTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <imgui.h>
#include <imgui_internal.h>

namespace {

namespace StackFormat = StackBinaryFormat;

constexpr ImVec4 kEditorWorkspaceBaseColor = ImVec4(0.016f, 0.231f, 0.274f, 1.0f);

struct DecodedImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
};

bool DecodeImageFromFile(const std::string& path, DecodedImageData& outImage) {
    outImage = {};

    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.originalChannels = channels;
    outImage.pixels.assign(pixels, pixels + (width * height * 4));
    stbi_image_free(pixels);
    return true;
}

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }

    const int safeChannels = std::max(1, channels);
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, safeChannels, pixels.data(), width * safeChannels);
    return pngBytes;
}

std::string SanitizeProjectFileStem(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            result.push_back('_');
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? std::string("project") : result;
}

nlohmann::json BuildDefaultIntegratedToneLayerJson() {
    ToneCurveLayer toneCurve;
    return toneCurve.Serialize();
}

std::string JoinSummaryList(const std::vector<std::string>& items) {
    if (items.empty()) {
        return {};
    }
    if (items.size() == 1) {
        return items.front();
    }
    if (items.size() == 2) {
        return items[0] + " and " + items[1];
    }

    std::string joined;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            joined += (index + 1 == items.size()) ? ", and " : ", ";
        }
        joined += items[index];
    }
    return joined;
}

std::string BuildHdrMergeCaptureMetadataSummary(const Raw::RawMetadata& metadata, float developExposureStops) {
    std::vector<std::string> parts;
    parts.reserve(4);

    char buffer[64];
    if (metadata.hasExposureTime) {
        std::snprintf(buffer, sizeof(buffer), "%.4g s", std::max(0.0f, metadata.exposureTimeSeconds));
        parts.emplace_back(buffer);
    }
    if (metadata.hasApertureFNumber) {
        std::snprintf(buffer, sizeof(buffer), "f/%.1f", std::max(0.0f, metadata.apertureFNumber));
        parts.emplace_back(buffer);
    }
    if (metadata.hasIsoSpeed) {
        std::snprintf(buffer, sizeof(buffer), "ISO %.0f", std::max(0.0f, metadata.isoSpeed));
        parts.emplace_back(buffer);
    }
    if (std::fabs(developExposureStops) > 0.001f) {
        std::snprintf(buffer, sizeof(buffer), "Develop %+.2f EV", developExposureStops);
        parts.emplace_back(buffer);
    }

    std::string summary;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            summary += "  ";
        }
        summary += parts[index];
    }
    return summary;
}

bool HasHdrMergeCaptureExposureMetadata(const Raw::RawMetadata& metadata) {
    return metadata.hasExposureTime || metadata.hasIsoSpeed || metadata.hasApertureFNumber;
}

float ComputeHdrMergeCaptureExposureEvForStatus(const Raw::RawMetadata& metadata) {
    const float shutter = metadata.hasExposureTime ? std::max(0.000001f, metadata.exposureTimeSeconds) : 1.0f;
    const float isoFactor = metadata.hasIsoSpeed ? std::max(0.01f, metadata.isoSpeed / 100.0f) : 1.0f;
    const float aperture = metadata.hasApertureFNumber ? std::max(0.1f, metadata.apertureFNumber) : 1.0f;
    return std::log2((shutter * isoFactor) / std::max(0.01f, aperture * aperture));
}

float MedianSummaryFloat(std::vector<float> values) {
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

float SelectHdrMergeExposureAnchorForStatus(
    const std::array<float, 3>& absoluteExposureEv,
    const std::array<bool, 3>& activeInputs) {
    std::vector<float> activeValues;
    activeValues.reserve(3);
    for (int i = 0; i < 3; ++i) {
        if (activeInputs[i]) {
            activeValues.push_back(absoluteExposureEv[i]);
        }
    }
    if (activeValues.empty()) {
        return 0.0f;
    }

    const float median = MedianSummaryFloat(activeValues);
    float bestDistance = std::numeric_limits<float>::infinity();
    float anchor = activeValues.front();
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            continue;
        }
        const float distance = std::fabs(absoluteExposureEv[i] - median);
        if (distance < bestDistance) {
            bestDistance = distance;
            anchor = absoluteExposureEv[i];
        }
    }
    return anchor;
}

bool HasBlockingModalPopupOpen() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context) {
        return false;
    }

    for (int i = context->OpenPopupStack.Size - 1; i >= 0; --i) {
        const ImGuiPopupData& popup = context->OpenPopupStack[i];
        if (popup.Window && (popup.Window->Flags & ImGuiWindowFlags_Modal) != 0) {
            return true;
        }
    }
    return false;
}

void CloseNonModalPopupsForToolbarSwitch() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context || context->OpenPopupStack.empty() || HasBlockingModalPopupOpen()) {
        return;
    }
    ImGui::ClosePopupToLevel(0, true);
}

void ResizeNearestRgba(
    const std::vector<unsigned char>& srcPixels,
    int srcW,
    int srcH,
    int dstW,
    int dstH,
    std::vector<unsigned char>& dstPixels) {
    dstPixels.assign(static_cast<size_t>(dstW * dstH * 4), 0);
    if (srcPixels.empty() || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return;
    }

    for (int y = 0; y < dstH; ++y) {
        const int srcY = std::clamp(static_cast<int>((static_cast<float>(y) / static_cast<float>(dstH)) * srcH), 0, srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            const int srcX = std::clamp(static_cast<int>((static_cast<float>(x) / static_cast<float>(dstW)) * srcW), 0, srcW - 1);
            const size_t dstIndex = static_cast<size_t>((y * dstW + x) * 4);
            const size_t srcIndex = static_cast<size_t>((srcY * srcW + srcX) * 4);
            dstPixels[dstIndex + 0] = srcPixels[srcIndex + 0];
            dstPixels[dstIndex + 1] = srcPixels[srcIndex + 1];
            dstPixels[dstIndex + 2] = srcPixels[srcIndex + 2];
            dstPixels[dstIndex + 3] = srcPixels[srcIndex + 3];
        }
    }
}

std::vector<unsigned char> BuildThumbnailBytes(
    const std::vector<unsigned char>& rgbaPixels,
    int width,
    int height) {
    if (rgbaPixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    constexpr int kMaxEdge = 320;
    if (width <= kMaxEdge && height <= kMaxEdge) {
        return EncodePngBytes(rgbaPixels, width, height, 4);
    }

    const float scale = static_cast<float>(kMaxEdge) / static_cast<float>(std::max(width, height));
    const int thumbW = std::max(1, static_cast<int>(std::floor(static_cast<float>(width) * scale)));
    const int thumbH = std::max(1, static_cast<int>(std::floor(static_cast<float>(height) * scale)));
    std::vector<unsigned char> thumbPixels;
    ResizeNearestRgba(rgbaPixels, width, height, thumbW, thumbH, thumbPixels);
    return EncodePngBytes(thumbPixels, thumbW, thumbH, 4);
}

std::string BuildTimestampString() {
    std::time_t now = std::time(nullptr);
    std::tm timeInfo{};
#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    timeInfo = *std::localtime(&now);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo) == 0) {
        return {};
    }
    return std::string(buffer);
}

std::vector<unsigned char> EncodePngBytesForImageStorage(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return {};
    }

    std::vector<unsigned char> topLeftPixels = bottomLeftPixels;
    LibraryManager::FlipImageRowsInPlace(topLeftPixels, width, height, std::max(1, channels));
    return EncodePngBytes(topLeftPixels, width, height, channels);
}

std::string FileNameFromPath(const std::string& path);

EditorNodeGraph::ImagePayload BuildImagePayloadFromDecoded(
    const std::string& path,
    DecodedImageData decoded) {
    EditorNodeGraph::ImagePayload payload;
    payload.label = FileNameFromPath(path);
    payload.sourcePath = path;
    payload.width = decoded.width;
    payload.height = decoded.height;
    payload.channels = decoded.channels;
    payload.originalChannels = decoded.originalChannels;
    payload.pngBytes = EncodePngBytesForImageStorage(decoded.pixels, decoded.width, decoded.height, decoded.channels);
    payload.pixels = std::move(decoded.pixels);
    return payload;
}

EditorNodeGraph::RawSourcePayload BuildRawPayloadFromMetadata(
    const std::string& path,
    Raw::RawMetadata metadata) {
    EditorNodeGraph::RawSourcePayload payload;
    payload.label = FileNameFromPath(path).empty() ? "RAW" : FileNameFromPath(path);
    payload.sourcePath = path;
    payload.metadata = std::move(metadata);
    payload.metadata.sourcePath = path;
    return payload;
}

Raw::RawDevelopSettings BuildRawDevelopSettingsFromMetadata(const Raw::RawMetadata& metadata) {
    Raw::RawDevelopSettings settings;
    if (metadata.hasDngBaselineExposure) {
        settings.exposureStops = metadata.dngBaselineExposure;
    }
    settings.blackLevelOverride = metadata.blackLevel;
    settings.whiteLevelOverride = metadata.whiteLevel;
    const bool dngHasColorTags = metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2 ||
        metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2;
    const bool dngHasCalibrationTags = metadata.hasDngCameraCalibration1 || metadata.hasDngCameraCalibration2;
    const bool dngHasDualForwardMatrices = metadata.hasDngForwardMatrix1 && metadata.hasDngForwardMatrix2;
    // Stack's own DNG dual-illuminant blend is still approximate; prefer LibRaw
    // when it has a matrix and the DNG does not provide calibration metadata.
    const bool preferLibRawForUnderSpecifiedDng =
        metadata.isDng &&
        metadata.hasCameraMatrix &&
        dngHasDualForwardMatrices &&
        !dngHasCalibrationTags;
    if (!metadata.isDng || preferLibRawForUnderSpecifiedDng) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    } else if (dngHasColorTags) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::DngAuto;
    } else {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    }
    return settings;
}

struct DevelopToneAutoStats {
    bool valid = false;
    float shadowPercentile = 0.02f;
    float midtonePercentile = 0.18f;
    float highlightPercentile = 0.85f;
    float clippingRatio = 0.0f;
    float noiseRisk = 0.0f;
    float highlightPressure = 0.0f;
    float textureConfidence = 0.5f;
    float hdrSpreadEv = 0.0f;
    float recommendedBaseEv = 0.0f;
    float recommendedLocalStrength = 1.05f;
    float recommendedShadowOpening = 1.20f;
    float recommendedHighlightCompression = 1.25f;
    int sceneProfile = 0;
};

enum class DevelopAutoSceneProfile : int {
    Balanced = 0,
    HighlightHeavy = 1,
    ShadowHeavy = 2,
    Flat = 3,
    NoisyLowLight = 4
};

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

DevelopAutoSceneProfile ResolveDevelopAutoSceneProfile(int profile) {
    return static_cast<DevelopAutoSceneProfile>(std::clamp(
        profile,
        static_cast<int>(DevelopAutoSceneProfile::Balanced),
        static_cast<int>(DevelopAutoSceneProfile::NoisyLowLight)));
}

DevelopToneAutoStats ReadDevelopToneAutoStats(const nlohmann::json& toneJson) {
    DevelopToneAutoStats stats;
    if (!toneJson.is_object()) {
        return stats;
    }

    stats.valid = toneJson.value("autoSceneStatsValid", false);
    stats.shadowPercentile = toneJson.value("autoSceneShadowPercentile", stats.shadowPercentile);
    stats.midtonePercentile = toneJson.value("autoSceneMidtonePercentile", stats.midtonePercentile);
    stats.highlightPercentile = toneJson.value("autoSceneHighlightPercentile", stats.highlightPercentile);
    stats.clippingRatio = toneJson.value("autoSceneClippingRatio", stats.clippingRatio);
    stats.noiseRisk = toneJson.value("autoSceneNoiseRisk", stats.noiseRisk);
    stats.highlightPressure = toneJson.value("autoSceneHighlightPressure", stats.highlightPressure);
    stats.textureConfidence = toneJson.value("autoSceneTextureConfidence", stats.textureConfidence);
    stats.hdrSpreadEv = toneJson.value("autoSceneHdrSpreadEv", stats.hdrSpreadEv);
    stats.recommendedBaseEv = toneJson.value("autoRecommendedBaseEv", stats.recommendedBaseEv);
    stats.recommendedLocalStrength = toneJson.value("autoRecommendedLocalStrength", stats.recommendedLocalStrength);
    stats.recommendedShadowOpening = toneJson.value("autoRecommendedShadowOpening", stats.recommendedShadowOpening);
    stats.recommendedHighlightCompression = toneJson.value("autoRecommendedHighlightCompression", stats.recommendedHighlightCompression);
    stats.sceneProfile = toneJson.value("autoSceneProfile", 0);
    return stats;
}

void ClampRawMosaicDenoiseSettings(Raw::RawMosaicDenoiseSettings& settings) {
    settings.hotPixelThreshold = std::clamp(settings.hotPixelThreshold, 0.01f, 0.50f);
    settings.lumaStrength = std::clamp(settings.lumaStrength, 0.0f, 1.0f);
    settings.chromaStrength = std::clamp(settings.chromaStrength, 0.0f, 1.0f);
    settings.radius = std::clamp(settings.radius, 1, 5);
    settings.edgeProtection = std::clamp(settings.edgeProtection, 0.0f, 1.0f);
    settings.iterations = std::clamp(settings.iterations, 1, 4);
}

void ClampIntegratedDevelopScenePrepSettings(Raw::RawDetailFusionSettings& settings) {
    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
    settings.invertMask = false;
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.minEv = std::clamp(settings.minEv, -2.5f, 0.5f);
    settings.maxEv = std::clamp(settings.maxEv, std::max(settings.minEv + 0.01f, 0.25f), 2.5f);
    settings.baseEv = std::clamp(settings.baseEv, -1.0f, 1.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.baseRadiusPercent = std::clamp(settings.baseRadiusPercent, 0.002f, 0.030f);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.10f, 0.55f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = 0.0f;
}

bool HasMeaningfulRawWhiteBalanceMetadata(const Raw::RawMetadata& metadata) {
    return metadata.cameraWhiteBalance[0] > 0.001f &&
        metadata.cameraWhiteBalance[1] > 0.001f &&
        metadata.cameraWhiteBalance[2] > 0.001f &&
        metadata.cameraWhiteBalance[3] > 0.001f;
}

bool HasMeaningfulRawDaylightWhiteBalanceMetadata(const Raw::RawMetadata& metadata) {
    return metadata.daylightWhiteBalance[0] > 0.001f &&
        metadata.daylightWhiteBalance[1] > 0.001f &&
        metadata.daylightWhiteBalance[2] > 0.001f &&
        metadata.daylightWhiteBalance[3] > 0.001f;
}

std::array<float, 3> NormalizeRawWhiteBalanceTriplet(const std::array<float, 4>& values) {
    const float green = std::max(0.001f, values[1]);
    return {
        std::max(0.001f, values[0]) / green,
        1.0f,
        std::max(0.001f, values[2]) / green
    };
}

float RawWhiteBalanceTripletDistance(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b) {
    return
        std::fabs(std::log2(std::max(0.001f, a[0]) / std::max(0.001f, b[0]))) +
        std::fabs(std::log2(std::max(0.001f, a[2]) / std::max(0.001f, b[2])));
}

bool TryResolveDevelopWhiteBalanceProbeMode(
    const std::string& candidateId,
    Raw::WhiteBalanceMode& outMode) {
    if (candidateId == "wbNeutralCorrection") {
        outMode = Raw::WhiteBalanceMode::Neutral;
        return true;
    }
    if (candidateId == "wbDaylightCorrection") {
        outMode = Raw::WhiteBalanceMode::Auto;
        return true;
    }
    if (candidateId == "wbCameraMood") {
        outMode = Raw::WhiteBalanceMode::AsShot;
        return true;
    }
    return false;
}

bool IsDevelopWhiteBalanceProbeCandidateId(const std::string& candidateId) {
    Raw::WhiteBalanceMode mode = Raw::WhiteBalanceMode::AsShot;
    return TryResolveDevelopWhiteBalanceProbeMode(candidateId, mode);
}

void ApplyDevelopToneGuidanceToJson(
    nlohmann::json& toneJson,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopAutoIntent intent) {
    toneJson["autoIntent"] = EditorNodeGraph::DevelopAutoIntentStableString(intent);
    toneJson["autoSceneAssistStrength"] = guidance.autoStrength;
    toneJson["autoBrightnessIntent"] = guidance.exposureBias;
    toneJson["autoRawExposurePreferenceEv"] = guidance.exposureBias * 2.0f;
    toneJson["autoDynamicRange"] = guidance.dynamicRange;
    toneJson["autoShadowBias"] = guidance.shadowLift;
    toneJson["autoHighlightBias"] = guidance.highlightGuard;
    toneJson["autoHighlightCharacter"] = guidance.highlightCharacter;
    toneJson["autoContrastBias"] = guidance.contrastBias;
    toneJson["autoSubjectSceneBias"] = guidance.subjectSceneBias;
    toneJson["autoMoodReadabilityBias"] = guidance.moodReadabilityBias;
}

void QueueDevelopToneCalibration(nlohmann::json& toneJson) {
    const std::uint64_t requestId = toneJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
    toneJson["autoCalibratePending"] = true;
    toneJson["autoCalibrateVariant"] = 0;
    toneJson["autoCalibrateRequestId"] = requestId + 1;
}

struct DevelopAutoIntentProfile {
    float autoStrengthScale = 1.0f;
    float autoStrengthBias = 0.0f;
    float exposureBias = 0.0f;
    float dynamicRangeBias = 0.0f;
    float shadowLiftBias = 0.0f;
    float highlightGuardBias = 0.0f;
    float highlightCharacterBias = 0.0f;
    float contrastBias = 0.0f;
    float rawExposureBias = 0.0f;
    float rawLiftScale = 1.0f;
    float rawHighlightRecoveryBias = 0.0f;
    float rawNoiseBias = 0.0f;
    float prepStrengthBias = 0.0f;
    float prepShadowBias = 0.0f;
    float prepHighlightBias = 0.0f;
    float prepContrastLift = 0.0f;
    float prepNoiseBias = 0.0f;
};

DevelopAutoIntentProfile ResolveDevelopAutoIntentProfile(EditorNodeGraph::DevelopAutoIntent intent) {
    DevelopAutoIntentProfile profile;
    switch (intent) {
        case EditorNodeGraph::DevelopAutoIntent::CleanBase:
            // Keep the data tidy and editable, with less final-tone commitment.
            profile.autoStrengthScale = 0.90f;
            profile.dynamicRangeBias = -0.05f;
            profile.highlightGuardBias = 0.08f;
            profile.contrastBias = -0.16f;
            profile.rawNoiseBias = 0.06f;
            profile.prepStrengthBias = -0.08f;
            profile.prepNoiseBias = 0.06f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::FlatEditingBase:
            // Open useful mids/range for manual work without pretending it is a log dump.
            profile.autoStrengthBias = 0.05f;
            profile.dynamicRangeBias = 0.38f;
            profile.shadowLiftBias = 0.24f;
            profile.highlightGuardBias = 0.20f;
            profile.contrastBias = -0.38f;
            profile.rawLiftScale = 1.05f;
            profile.prepStrengthBias = 0.08f;
            profile.prepShadowBias = 0.18f;
            profile.prepHighlightBias = 0.16f;
            profile.prepContrastLift = -0.16f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::BrightNatural:
            // Brighter render intent should land mostly through placement, not reckless clipping.
            profile.exposureBias = 0.16f;
            profile.shadowLiftBias = 0.10f;
            profile.highlightGuardBias = 0.08f;
            profile.contrastBias = -0.04f;
            profile.rawExposureBias = 0.12f;
            profile.prepShadowBias = 0.08f;
            profile.prepContrastLift = 0.04f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::DarkNatural:
            // Preserve low-key mood and avoid forcing dark scenes into gray mids.
            profile.exposureBias = -0.16f;
            profile.shadowLiftBias = -0.18f;
            profile.highlightGuardBias = 0.04f;
            profile.contrastBias = 0.06f;
            profile.rawExposureBias = -0.14f;
            profile.rawLiftScale = 0.82f;
            profile.prepStrengthBias = -0.08f;
            profile.prepShadowBias = -0.18f;
            profile.prepContrastLift = -0.05f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast:
            // Add separation and endpoint confidence while still using highlight safeguards.
            profile.autoStrengthScale = 1.05f;
            profile.dynamicRangeBias = -0.12f;
            profile.shadowLiftBias = -0.16f;
            profile.highlightGuardBias = 0.06f;
            profile.highlightCharacterBias = 0.20f;
            profile.contrastBias = 0.36f;
            profile.rawLiftScale = 0.92f;
            profile.prepStrengthBias = 0.03f;
            profile.prepShadowBias = -0.10f;
            profile.prepContrastLift = 0.18f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail:
            // Fit more visible range while keeping language honest about clipped data.
            profile.autoStrengthBias = 0.10f;
            profile.dynamicRangeBias = 0.58f;
            profile.shadowLiftBias = 0.34f;
            profile.highlightGuardBias = 0.42f;
            profile.highlightCharacterBias = -0.10f;
            profile.contrastBias = -0.24f;
            profile.rawLiftScale = 1.08f;
            profile.rawHighlightRecoveryBias = 0.14f;
            profile.prepStrengthBias = 0.14f;
            profile.prepShadowBias = 0.26f;
            profile.prepHighlightBias = 0.28f;
            profile.prepContrastLift = -0.10f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::NaturalFinished:
        default:
            break;
    }
    return profile;
}

EditorNodeGraph::DevelopAutoGuidance BuildModeAwareDevelopGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopAutoIntentProfile& profile) {
    EditorNodeGraph::DevelopAutoGuidance effective = guidance;
    effective.autoStrength = std::clamp(
        guidance.autoStrength * profile.autoStrengthScale + profile.autoStrengthBias,
        0.0f,
        2.4f);
    effective.exposureBias = std::clamp(guidance.exposureBias + profile.exposureBias, -2.0f, 2.0f);
    effective.dynamicRange = std::clamp(guidance.dynamicRange + profile.dynamicRangeBias, 0.25f, 3.0f);
    effective.shadowLift = std::clamp(guidance.shadowLift + profile.shadowLiftBias, -1.25f, 1.25f);
    effective.highlightGuard = std::clamp(guidance.highlightGuard + profile.highlightGuardBias, -1.25f, 1.25f);
    effective.highlightCharacter = std::clamp(guidance.highlightCharacter + profile.highlightCharacterBias, -1.25f, 1.25f);
    effective.contrastBias = std::clamp(guidance.contrastBias + profile.contrastBias, -1.25f, 1.25f);
    effective.subjectSceneBias = std::clamp(guidance.subjectSceneBias, -1.0f, 1.0f);
    effective.moodReadabilityBias = std::clamp(guidance.moodReadabilityBias, -1.0f, 1.0f);
    return effective;
}

struct DevelopAutoCandidateSolve {
    std::string id;
    std::string label;
    std::string reason;
    EditorNodeGraph::DevelopAutoGuidance guidance;
    std::uint64_t guidanceFingerprint = 0;
    float score = 0.0f;
    nlohmann::json scoreComponents = nlohmann::json::object();
    bool continuationBiasActive = false;
    float continuationBiasBonus = 0.0f;
    std::string continuationBiasReason;
    std::string continuationBiasStage;
    std::string continuationBiasRefineIntent;
    bool continuationExpansionCandidate = false;
    std::string continuationExpansionReason;
    std::string continuationExpansionStage;
    std::string continuationExpansionRefineIntent;
    bool rejected = false;
    bool duplicate = false;
    bool rememberedRejection = false;
    bool renderedMemoryRejected = false;
    bool whiteBalanceProbe = false;
    Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
    std::string rejectReason;
};

struct DevelopAutoCandidateSolveResult {
    std::vector<DevelopAutoCandidateSolve> candidates;
    EditorNodeGraph::DevelopAutoGuidance authoredGuidance;
    std::string selectedId = "base";
    std::string selectedLabel = "Base Solve";
    float selectedScore = 0.0f;
    std::string selectionSource = "parameterScore";
    bool mergeApplied = false;
    std::string mergeFirstId;
    std::string mergeSecondId;
    std::string mergeThirdId;
    float mergeFirstWeight = 1.0f;
    float mergeSecondWeight = 0.0f;
    float mergeThirdWeight = 0.0f;
    bool renderedFeedbackApplied = false;
    std::uint64_t renderedFeedbackSourceFingerprint = 0;
    int renderedFeedbackPass = 0;
    std::string renderedFeedbackPreviousSelectedId;
    float renderedFeedbackPreviousSelectedScore = 0.0f;
    std::string renderedFeedbackBestId;
    float renderedFeedbackBestScore = 0.0f;
    std::string renderedFeedbackAction;
    std::string renderedFeedbackRefineIntent;
    std::string renderedFeedbackRefineReason;
    std::string renderedFeedbackRevisionStage;
    std::string renderedFeedbackRevisionReason;
    std::string renderedFeedbackStopReason;
    bool renderedFeedbackStopIsConverged = false;
    float renderedFeedbackImprovement = -1.0f;
    float renderedFeedbackAdmissionBaseMinimumImprovement = 0.025f;
    float renderedFeedbackAdmissionMinimumImprovement = 0.025f;
    bool renderedFeedbackAdmissionTightened = false;
    std::string renderedFeedbackAdmissionReason = "base";
    std::string renderedFeedbackAdmissionEvidenceState;
    std::string renderedFeedbackAdmissionEvidenceDecision;
    int renderedFeedbackAdmissionEvidencePass = 0;
    float renderedFeedbackStabilityDistance = -1.0f;
    float renderedFeedbackStabilityScoreDelta = -1.0f;
    std::string renderedFeedbackStabilityReferenceId;
    int renderedFeedbackTrendHistoryCount = 0;
    int renderedFeedbackTrendSameBestCount = 0;
    float renderedFeedbackTrendScoreSpread = -1.0f;
    float renderedFeedbackTrendNearestDistance = -1.0f;
    std::string renderedFeedbackTrendReferenceId;
    std::string renderedFeedbackMonotonicMetric;
    float renderedFeedbackMonotonicPreviousValue = -1.0f;
    float renderedFeedbackMonotonicCurrentValue = -1.0f;
    std::string renderedFeedbackMonotonicReferenceId;
    int renderedFeedbackCarriedForwardCount = 0;
    std::uint64_t candidateContextFingerprint = 0;
    bool continuationBiasActive = false;
    std::string continuationBiasReason;
    std::string continuationBiasDecision;
    std::string continuationBiasStage;
    std::string continuationBiasRefineIntent;
    int continuationBiasAppliedCount = 0;
    bool continuationExpansionEligible = false;
    std::string continuationExpansionReason;
    std::string continuationExpansionStage;
    std::string continuationExpansionRefineIntent;
    int continuationExpansionAddedCount = 0;
    int rejectedMemorySuppressionCount = 0;
    int renderedRejectedMemorySuppressionCount = 0;
    nlohmann::json dynamicRangeStrategy = nlohmann::json::object();
    nlohmann::json subjectSceneIntent = nlohmann::json::object();
    bool authoredWhiteBalanceProbe = false;
    Raw::WhiteBalanceMode authoredWhiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
    bool converged = false;
    int convergencePass = 0;
    std::uint64_t fingerprint = 0;
};

void SetDevelopResultWhiteBalanceProbe(
    DevelopAutoCandidateSolveResult& result,
    const DevelopAutoCandidateSolve& candidate) {
    result.authoredWhiteBalanceProbe = candidate.whiteBalanceProbe;
    result.authoredWhiteBalanceMode = candidate.whiteBalanceMode;
}

void ClearDevelopResultWhiteBalanceProbe(DevelopAutoCandidateSolveResult& result) {
    result.authoredWhiteBalanceProbe = false;
    result.authoredWhiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
}

constexpr int kDevelopRenderedFeedbackMaxPasses = 3;
constexpr const char* kDevelopRenderedFeedbackLoopVersion = "RenderedFeedbackLoopV1";
constexpr const char* kDevelopRenderedContinuationVersion = "RenderedContinuationV1";
constexpr const char* kDevelopConvergenceEvidenceVersion = "ConvergenceEvidenceV1";
constexpr const char* kDevelopConvergenceAdmissionVersion = "ConvergenceAdmissionV1";
constexpr const char* kDevelopContinuationCandidateBiasVersion = "ContinuationCandidateBiasV1";
constexpr const char* kDevelopContinuationCandidateExpansionVersion = "ContinuationCandidateExpansionV1";
constexpr const char* kDevelopDynamicRangeStrategyVersion = "DynamicRangeStrategyV1";
constexpr const char* kDevelopDynamicRangeStrategyMapVersion = "DynamicRangeStrategyMapV1";
constexpr const char* kDevelopLocalExposureStrategyVersion = "LocalExposureStrategyV1";
constexpr const char* kDevelopDynamicRangeRegionEvidenceVersion = "DynamicRangeRegionEvidenceV1";
constexpr const char* kDevelopSubjectSceneIntentVersion = "SubjectSceneIntentV1";
constexpr const char* kDevelopSubjectImportanceMapVersion = "SubjectImportanceMapV1";
constexpr const char* kDevelopSubjectRefinedMapVersion = "SubjectRefinedMapV1";
constexpr const char* kDevelopSubjectImportanceSolveNotesVersion = "SubjectImportanceSolveNotesV1";
constexpr int kDevelopSubjectImportanceMapGridSize = 5;
constexpr int kDevelopSubjectImportanceMapCellCount =
    kDevelopSubjectImportanceMapGridSize * kDevelopSubjectImportanceMapGridSize;

float DevelopRegionRiskAbove(float value, float safeValue, float fullRiskValue) {
    if (fullRiskValue <= safeValue) {
        return value > safeValue ? 1.0f : 0.0f;
    }
    return SaturateFloat((value - safeValue) / (fullRiskValue - safeValue));
}

float DevelopRegionRiskBelow(float value, float safeValue, float fullRiskValue) {
    if (safeValue <= fullRiskValue) {
        return value < safeValue ? 1.0f : 0.0f;
    }
    return SaturateFloat((safeValue - value) / (safeValue - fullRiskValue));
}

struct DevelopDynamicRangeRegionEvidence {
    bool valid = false;
    std::string source = "unavailable";
    std::string candidateId;
    float renderScore = -1.0f;
    float localHighlightPressure = 0.0f;
    float localShadowPressure = 0.0f;
    float localDamageRiskMean = 0.0f;
    float localDamageRiskPeak = 0.0f;
    float localLumaSpread = 0.0f;
    float localEvSpreadStops = 0.0f;
    float localEvConflict = 0.0f;
    float localContrastPeak = 0.0f;
    float centerShadowFraction = 0.0f;
    float centerHighlightFraction = 0.0f;
    float clippedFraction = 0.0f;
    float highlightFraction = 0.0f;
    float shadowFraction = 0.0f;
    float shadowTextureRisk = 0.0f;
    float haloRiskFraction = 0.0f;
    float lowSaturationFraction = 0.0f;
    float highlightBandFraction = 0.0f;
    float highlightMeanLuma = 0.0f;
    float highlightLowSaturationFraction = 0.0f;
    float highlightGrayRisk = 0.0f;
    float highlightTileCoverage = 0.0f;
    float highlightStructureScore = 0.0f;
    float meaningfulHighlightPressure = 0.0f;
    float contrastSpan = 0.0f;
    int peakTile = -1;
    float broadHighlightPressure = 0.0f;
    float localHighlightHotspotRisk = 0.0f;
    float localShadowHotspotRisk = 0.0f;
    float shadowNoiseLiftRisk = 0.0f;
    float localHaloRisk = 0.0f;
    float flatGrayRisk = 0.0f;
    float localRangeConflict = 0.0f;
    float brightnessHierarchyRisk = 0.0f;
    float localExposureHighlightCrowding = 0.0f;
    float localExposureShadowCrowding = 0.0f;
    float localExposureHaloStress = 0.0f;
    float localExposureFlatnessRisk = 0.0f;
    float localExposureDamageRisk = 0.0f;
    float subjectCenterPrior = 0.0f;
    float subjectReadabilityPressure = 0.0f;
    float subjectProtectionPressure = 0.0f;
    float subjectMoodPreservationPressure = 0.0f;
    float subjectImportanceConfidence = 0.0f;
    bool smallSpecularLikely = false;
};

struct DevelopSubjectSceneIntent {
    std::string id = "automaticSceneBalance";
    std::string label = "Automatic Scene Balance";
    std::string reason = "No user importance brush is active; Auto is using weak composition and rendered evidence only.";
    std::string userGuidanceStatus = "notAvailable";
    bool userGuidanceActive = false;
    bool automaticOnly = true;
    float userSubjectSceneBias = 0.0f;
    float userMoodReadabilityBias = 0.0f;
    float userGuidanceStrength = 0.0f;
    float automaticConfidence = 0.0f;
    float centerPrior = 0.0f;
    float readabilityPressure = 0.0f;
    float protectionPressure = 0.0f;
    float moodPreservationPressure = 0.0f;
    float subjectPriority = 0.5f;
    float sceneIntegrity = 0.5f;
    float improveReadability = 0.5f;
    float preserveMood = 0.5f;
    float subjectSceneAxis = 0.0f;
    float moodReadabilityAxis = 0.0f;
    int importanceRegionCount = 0;
    int importanceStrokeCount = 0;
    float importanceStrength = 0.0f;
    float importanceImportant = 0.0f;
    float importanceReveal = 0.0f;
    float importanceProtect = 0.0f;
    float importancePreserveMood = 0.0f;
    float importanceIgnore = 0.0f;
    float importanceSubjectPriority = 0.0f;
    float importanceReadability = 0.0f;
    float importanceProtection = 0.0f;
    float importanceMood = 0.0f;
    float importanceLowPriority = 0.0f;
    nlohmann::json importanceMap = nlohmann::json::object();
    float importanceMapCoverage = 0.0f;
    float importanceMapPositiveCoverage = 0.0f;
    float importanceMapLowPriorityCoverage = 0.0f;
    float importanceMapRevealCoverage = 0.0f;
    float importanceMapProtectCoverage = 0.0f;
    float importanceMapMoodCoverage = 0.0f;
    float importanceMapPeak = 0.0f;
    float importanceMapConfidence = 0.0f;
    float importanceMapCenterBias = 0.0f;
    float importanceMapEdgeBias = 0.0f;
    nlohmann::json refinedImportanceMap = nlohmann::json::object();
    float refinedMapCoverage = 0.0f;
    float refinedMapLowPriorityCoverage = 0.0f;
    float refinedMapReadabilityCoverage = 0.0f;
    float refinedMapProtectionCoverage = 0.0f;
    float refinedMapMoodCoverage = 0.0f;
    float refinedMapPeak = 0.0f;
    float refinedMapConfidence = 0.0f;
    float refinedMapBoundaryHint = 0.0f;
    nlohmann::json solveNotes = nlohmann::json::array();
    nlohmann::json evidence = nlohmann::json::object();
};

struct DevelopSubjectImportanceSummary {
    bool enabled = false;
    int activeRegionCount = 0;
    int activeStrokeCount = 0;
    float strength = 0.0f;
    float important = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float ignore = 0.0f;
    float subjectPriority = 0.0f;
    float readability = 0.0f;
    float protection = 0.0f;
    float mood = 0.0f;
    float lowPriority = 0.0f;
};

struct DevelopSubjectImportanceMapCell {
    float importance = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;
};

struct DevelopSubjectRefinedMapCell {
    float importance = 0.0f;
    float confidence = 0.0f;
    float readability = 0.0f;
    float protection = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;
    float boundaryHint = 0.0f;
};

struct DevelopSubjectImportanceInterpretation {
    bool enabled = false;
    bool active = false;
    std::string status = "disabled";
    std::string reason = "Subject importance guidance is disabled.";
    int gridWidth = kDevelopSubjectImportanceMapGridSize;
    int gridHeight = kDevelopSubjectImportanceMapGridSize;
    int activeRegionCount = 0;
    int activeStrokeCount = 0;
    float coverage = 0.0f;
    float positiveCoverage = 0.0f;
    float lowPriorityCoverage = 0.0f;
    float revealCoverage = 0.0f;
    float protectCoverage = 0.0f;
    float moodCoverage = 0.0f;
    float peakImportance = 0.0f;
    float meanImportance = 0.0f;
    float centerBias = 0.0f;
    float edgeBias = 0.0f;
    float mapConfidence = 0.0f;
    std::array<DevelopSubjectImportanceMapCell, kDevelopSubjectImportanceMapCellCount> cells {};
};

struct DevelopSubjectRefinedMap {
    bool enabled = false;
    bool active = false;
    std::string status = "disabled";
    std::string reason = "Subject importance guidance is disabled.";
    std::string sourceMapVersion = kDevelopSubjectImportanceMapVersion;
    int gridWidth = kDevelopSubjectImportanceMapGridSize;
    int gridHeight = kDevelopSubjectImportanceMapGridSize;
    float coverage = 0.0f;
    float lowPriorityCoverage = 0.0f;
    float readabilityCoverage = 0.0f;
    float protectionCoverage = 0.0f;
    float moodCoverage = 0.0f;
    float peakImportance = 0.0f;
    float meanConfidence = 0.0f;
    float boundaryHint = 0.0f;
    std::array<DevelopSubjectRefinedMapCell, kDevelopSubjectImportanceMapCellCount> cells {};
};

struct DevelopDynamicRangeStrategy {
    std::string id = "balancedRange";
    std::string label = "Balanced Range";
    std::string reason = "The selected intent can use the balanced RAW, Scene Prep, and finish-tone range strategy.";
    std::string highlightPolicy = "Preserve meaningful highlights while allowing normal tiny specular clipping.";
    std::string shadowPolicy = "Open meaningful shadows only when noise and mode intent allow it.";
    std::string strategyMapReason = "Internal solver coordinates are balanced until rendered analysis has enough evidence.";
    std::string localExposureStrategyId = "balancedLocalPrep";
    std::string localExposureStrategyLabel = "Balanced Local Prep";
    std::string localExposureStrategyReason = "Use moderate local exposure shaping with existing halo, noise, and texture guardrails.";
    float highlightImportance = 0.0f;
    float shadowReadability = 0.0f;
    float noiseConstraint = 0.0f;
    float rangeCompression = 0.0f;
    float brightnessHierarchyRisk = 0.0f;
    float meaningfulHighlightPressure = 0.0f;
    float naturalContrastGuardNeed = 0.0f;
    float brightHighlightRolloffNeed = 0.0f;
    float highlightBrightnessAnchorNeed = 0.0f;
    float broadHighlightGuardNeed = 0.0f;
    float specularHighlightToleranceNeed = 0.0f;
    float shadowReadabilityLiftNeed = 0.0f;
    float shadowNoiseFloorNeed = 0.0f;
    float localHighlightHotspotRisk = 0.0f;
    float localShadowHotspotRisk = 0.0f;
    float localRangeConflict = 0.0f;
    float localEvConflict = 0.0f;
    float localHaloRisk = 0.0f;
    float localHaloGuardNeed = 0.0f;
    float flatGrayRisk = 0.0f;
    float highlightGrayRisk = 0.0f;
    float strategyMapHighlightShadowAxis = 0.0f;
    float strategyMapContrastRangeAxis = 0.0f;
    float strategyMapHighlightPriority = 0.5f;
    float strategyMapShadowVisibility = 0.5f;
    float strategyMapNaturalContrast = 0.5f;
    float strategyMapVisibleRange = 0.5f;
    float localExposureRangeRedistribution = 0.0f;
    float localExposureHighlightCompression = 0.0f;
    float localExposureShadowOpening = 0.0f;
    float localExposureNoiseGuard = 0.0f;
    float localExposureHaloGuard = 0.0f;
    float localExposureTextureGuard = 0.0f;
    float localExposureShadowEvBudget = 0.0f;
    float localExposureHighlightEvBudget = 0.0f;
    float localExposureStrengthTarget = 0.5f;
    float localExposureHighlightCrowding = 0.0f;
    float localExposureShadowCrowding = 0.0f;
    float localExposureHaloStress = 0.0f;
    float localExposureFlatnessRisk = 0.0f;
    float localExposureDamageRisk = 0.0f;
    bool smallSpecularClippingAllowed = false;
    nlohmann::json regionEvidence = nlohmann::json::object();
};

void ResolveDevelopDynamicRangeStrategyMap(
    DevelopDynamicRangeStrategy& strategy,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float shadowRescueNeed,
    float hdrNeed,
    float tinySpecularAllowance) {
    if (!stats.valid) {
        return;
    }

    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const float clippingPressure = SaturateFloat(stats.clippingRatio / 0.018f);

    const float highlightSidePressure = SaturateFloat(
        strategy.highlightImportance * 0.46f +
        strategy.broadHighlightGuardNeed * 0.18f +
        regionEvidence.meaningfulHighlightPressure * 0.14f +
        regionEvidence.broadHighlightPressure * 0.10f +
        regionEvidence.highlightGrayRisk * 0.08f +
        clippingPressure * 0.16f +
        (darkIntent ? 0.05f : 0.0f) +
        (rangeIntent ? 0.05f : 0.0f) -
        tinySpecularAllowance * 0.10f -
        strategy.specularHighlightToleranceNeed * 0.04f);
    const float shadowSidePressure = SaturateFloat(
        strategy.shadowReadability * 0.44f +
        strategy.shadowReadabilityLiftNeed * 0.22f +
        shadowRescueNeed * 0.16f +
        regionEvidence.localShadowHotspotRisk * 0.10f +
        std::max(0.0f, guidance.shadowLift) * 0.08f +
        (brightIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.06f : 0.0f) -
        strategy.shadowNoiseFloorNeed * 0.14f -
        strategy.noiseConstraint * 0.08f -
        (darkIntent ? 0.04f : 0.0f));

    const float visibleRangePressure = SaturateFloat(
        strategy.rangeCompression * 0.40f +
        hdrNeed * 0.22f +
        regionEvidence.localRangeConflict * 0.12f +
        regionEvidence.localEvConflict * 0.12f +
        strategy.broadHighlightGuardNeed * 0.07f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.10f +
        (rangeIntent ? 0.16f : 0.0f) +
        (flatIntent ? 0.08f : 0.0f) -
        strategy.naturalContrastGuardNeed * 0.04f -
        (punchyIntent ? 0.08f : 0.0f));
    const float naturalContrastPressure = SaturateFloat(
        strategy.naturalContrastGuardNeed * 0.36f +
        strategy.brightnessHierarchyRisk * 0.20f +
        regionEvidence.highlightGrayRisk * 0.10f +
        regionEvidence.flatGrayRisk * 0.08f +
        std::max(0.0f, guidance.contrastBias) * 0.08f +
        (punchyIntent ? 0.16f : 0.0f) +
        (naturalIntent ? 0.07f : 0.0f) +
        (darkIntent ? 0.07f : 0.0f) -
        (rangeIntent ? 0.12f : 0.0f) -
        (flatIntent ? 0.08f : 0.0f));

    strategy.strategyMapHighlightShadowAxis =
        std::clamp(shadowSidePressure - highlightSidePressure, -1.0f, 1.0f);
    strategy.strategyMapContrastRangeAxis =
        std::clamp(visibleRangePressure - naturalContrastPressure, -1.0f, 1.0f);
    strategy.strategyMapHighlightPriority =
        SaturateFloat(0.5f - strategy.strategyMapHighlightShadowAxis * 0.5f);
    strategy.strategyMapShadowVisibility =
        SaturateFloat(0.5f + strategy.strategyMapHighlightShadowAxis * 0.5f);
    strategy.strategyMapNaturalContrast =
        SaturateFloat(0.5f - strategy.strategyMapContrastRangeAxis * 0.5f);
    strategy.strategyMapVisibleRange =
        SaturateFloat(0.5f + strategy.strategyMapContrastRangeAxis * 0.5f);
    strategy.strategyMapReason =
        "Internal solver map: horizontal balances highlight priority against shadow visibility; vertical balances natural contrast against maximum visible range. Future graph controls can expose these coordinates without turning Auto into presets.";
}

void ResolveDevelopLocalExposureStrategy(
    DevelopDynamicRangeStrategy& strategy,
    EditorNodeGraph::DevelopAutoIntent intent,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float shadowRescueNeed,
    float hdrNeed) {
    if (!stats.valid) {
        strategy.localExposureStrategyId = "pendingLocalEvidence";
        strategy.localExposureStrategyLabel = "Pending Local Evidence";
        strategy.localExposureStrategyReason =
            "Develop needs rendered statistics before it can choose a local exposure strategy.";
        return;
    }

    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;

    strategy.localExposureRangeRedistribution = SaturateFloat(
        strategy.rangeCompression * 0.28f +
        strategy.strategyMapVisibleRange * 0.22f +
        regionEvidence.localRangeConflict * 0.16f +
        regionEvidence.localEvConflict * 0.16f +
        regionEvidence.localExposureHighlightCrowding * 0.08f +
        regionEvidence.localExposureShadowCrowding * 0.08f +
        hdrNeed * 0.16f +
        (rangeIntent ? 0.12f : 0.0f) +
        (flatIntent ? 0.08f : 0.0f) -
        regionEvidence.localExposureHaloStress * 0.08f -
        regionEvidence.localExposureDamageRisk * 0.06f -
        strategy.localHaloGuardNeed * 0.08f -
        (punchyIntent ? 0.06f : 0.0f));
    strategy.localExposureHighlightCompression = SaturateFloat(
        strategy.broadHighlightGuardNeed * 0.32f +
        strategy.highlightImportance * 0.18f +
        strategy.strategyMapHighlightPriority * 0.18f +
        regionEvidence.meaningfulHighlightPressure * 0.12f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        regionEvidence.localExposureHighlightCrowding * 0.16f +
        (rangeIntent ? 0.06f : 0.0f) -
        strategy.specularHighlightToleranceNeed * 0.10f);
    strategy.localExposureShadowOpening = SaturateFloat(
        strategy.shadowReadabilityLiftNeed * 0.32f +
        strategy.shadowReadability * 0.18f +
        strategy.strategyMapShadowVisibility * 0.18f +
        shadowRescueNeed * 0.16f +
        regionEvidence.localShadowHotspotRisk * 0.10f +
        regionEvidence.localExposureShadowCrowding * 0.14f +
        (brightIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.05f : 0.0f) -
        regionEvidence.localExposureDamageRisk * 0.08f -
        strategy.shadowNoiseFloorNeed * 0.18f -
        strategy.noiseConstraint * 0.10f -
        (darkIntent ? 0.05f : 0.0f));
    strategy.localExposureNoiseGuard = SaturateFloat(
        strategy.noiseConstraint * 0.34f +
        strategy.shadowNoiseFloorNeed * 0.28f +
        regionEvidence.shadowNoiseLiftRisk * 0.18f +
        regionEvidence.localExposureShadowCrowding * 0.10f +
        regionEvidence.localExposureDamageRisk * 0.06f +
        stats.noiseRisk * 0.14f -
        strategy.shadowReadabilityLiftNeed * 0.08f);
    strategy.localExposureHaloGuard = SaturateFloat(
        strategy.localHaloGuardNeed * 0.36f +
        regionEvidence.localHaloRisk * 0.24f +
        regionEvidence.localExposureHaloStress * 0.18f +
        regionEvidence.localExposureDamageRisk * 0.08f +
        regionEvidence.localRangeConflict * 0.12f +
        regionEvidence.localEvConflict * 0.10f +
        strategy.localExposureRangeRedistribution * 0.10f);
    strategy.localExposureTextureGuard = SaturateFloat(
        stats.textureConfidence * 0.28f +
        strategy.strategyMapNaturalContrast * 0.20f +
        strategy.naturalContrastGuardNeed * 0.16f +
        regionEvidence.localExposureFlatnessRisk * 0.12f +
        strategy.localExposureHaloGuard * 0.12f +
        strategy.localExposureNoiseGuard * 0.10f -
        strategy.localExposureRangeRedistribution * 0.08f);
    strategy.localExposureShadowEvBudget = std::clamp(
        0.18f +
            strategy.localExposureShadowOpening * 0.84f +
            strategy.localExposureRangeRedistribution * 0.42f -
            strategy.localExposureNoiseGuard * 0.38f -
            strategy.localExposureHaloGuard * 0.20f -
            regionEvidence.localExposureDamageRisk * 0.12f,
        0.0f,
        1.35f);
    strategy.localExposureHighlightEvBudget = std::clamp(
        0.12f +
            strategy.localExposureHighlightCompression * 0.76f +
            strategy.localExposureRangeRedistribution * 0.28f -
            strategy.specularHighlightToleranceNeed * 0.16f -
            regionEvidence.localExposureHaloStress * 0.08f,
        0.0f,
        1.25f);
    strategy.localExposureStrengthTarget = std::clamp(
        0.42f +
            strategy.localExposureRangeRedistribution * 0.24f +
            strategy.localExposureHighlightCompression * 0.10f +
            strategy.localExposureShadowOpening * 0.10f -
            strategy.localExposureHaloGuard * 0.08f -
            regionEvidence.localExposureDamageRisk * 0.06f -
            (punchyIntent ? 0.04f : 0.0f),
        0.25f,
        1.0f);

    if (strategy.localExposureHaloGuard > 0.58f) {
        strategy.localExposureStrategyId = "haloGuardedLocalPrep";
        strategy.localExposureStrategyLabel = "Halo-Guarded Local Prep";
        strategy.localExposureStrategyReason =
            "Local exposure has useful range pressure, but halo/edge evidence says Scene Prep should prioritize safer transitions.";
    } else if (strategy.localExposureHighlightCompression > 0.58f &&
        strategy.localExposureShadowOpening > 0.44f) {
        strategy.localExposureStrategyId = "highlightShadowBalance";
        strategy.localExposureStrategyLabel = "Highlight / Shadow Balance";
        strategy.localExposureStrategyReason =
            "Scene Prep should protect broad highlights while selectively opening readable shadows, keeping both moves local.";
    } else if (strategy.localExposureHighlightCompression > 0.56f) {
        strategy.localExposureStrategyId = "highlightLocalCompression";
        strategy.localExposureStrategyLabel = "Highlight Local Compression";
        strategy.localExposureStrategyReason =
            "Broad meaningful highlight evidence asks Scene Prep to compress visible bright regions without a global exposure cut.";
    } else if (strategy.localExposureShadowOpening > 0.56f &&
        strategy.localExposureNoiseGuard < 0.55f) {
        strategy.localExposureStrategyId = "readableShadowLocalLift";
        strategy.localExposureStrategyLabel = "Readable Shadow Local Lift";
        strategy.localExposureStrategyReason =
            "Shadow evidence is clean enough for Scene Prep to test local opening while RAW placement stays stable.";
    } else if (strategy.localExposureNoiseGuard > 0.58f || darkIntent) {
        strategy.localExposureStrategyId = "shadowFloorProtected";
        strategy.localExposureStrategyLabel = "Shadow Floor Protected";
        strategy.localExposureStrategyReason =
            "Noise or dark-scene intent says Scene Prep should avoid turning low-value dark areas into gray mush.";
    } else if (strategy.localExposureRangeRedistribution > 0.58f) {
        strategy.localExposureStrategyId = "rangeRedistribution";
        strategy.localExposureStrategyLabel = "Range Redistribution";
        strategy.localExposureStrategyReason =
            "Scene Prep should redistribute visible range locally while preserving believable lighting hierarchy.";
    }
}

nlohmann::json DevelopDynamicRangeRegionEvidenceToJson(
    const DevelopDynamicRangeRegionEvidence& evidence) {
    return {
        { "version", kDevelopDynamicRangeRegionEvidenceVersion },
        { "valid", evidence.valid },
        { "source", evidence.source },
        { "candidateId", evidence.candidateId },
        { "renderScore", evidence.renderScore },
        { "localHighlightPressure", evidence.localHighlightPressure },
        { "localShadowPressure", evidence.localShadowPressure },
        { "localDamageRiskMean", evidence.localDamageRiskMean },
        { "localDamageRiskPeak", evidence.localDamageRiskPeak },
        { "localLumaSpread", evidence.localLumaSpread },
        { "localEvSpreadStops", evidence.localEvSpreadStops },
        { "localEvConflict", evidence.localEvConflict },
        { "localContrastPeak", evidence.localContrastPeak },
        { "centerShadowFraction", evidence.centerShadowFraction },
        { "centerHighlightFraction", evidence.centerHighlightFraction },
        { "clippedFraction", evidence.clippedFraction },
        { "highlightFraction", evidence.highlightFraction },
        { "shadowFraction", evidence.shadowFraction },
        { "shadowTextureRisk", evidence.shadowTextureRisk },
        { "haloRiskFraction", evidence.haloRiskFraction },
        { "lowSaturationFraction", evidence.lowSaturationFraction },
        { "highlightBandFraction", evidence.highlightBandFraction },
        { "highlightMeanLuma", evidence.highlightMeanLuma },
        { "highlightLowSaturationFraction", evidence.highlightLowSaturationFraction },
        { "highlightGrayRisk", evidence.highlightGrayRisk },
        { "highlightTileCoverage", evidence.highlightTileCoverage },
        { "highlightStructureScore", evidence.highlightStructureScore },
        { "meaningfulHighlightPressure", evidence.meaningfulHighlightPressure },
        { "contrastSpan", evidence.contrastSpan },
        { "peakTile", evidence.peakTile },
        { "broadHighlightPressure", evidence.broadHighlightPressure },
        { "localHighlightHotspotRisk", evidence.localHighlightHotspotRisk },
        { "localShadowHotspotRisk", evidence.localShadowHotspotRisk },
        { "shadowNoiseLiftRisk", evidence.shadowNoiseLiftRisk },
        { "localHaloRisk", evidence.localHaloRisk },
        { "flatGrayRisk", evidence.flatGrayRisk },
        { "localRangeConflict", evidence.localRangeConflict },
        { "brightnessHierarchyRisk", evidence.brightnessHierarchyRisk },
        { "localExposureHighlightCrowding", evidence.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", evidence.localExposureShadowCrowding },
        { "localExposureHaloStress", evidence.localExposureHaloStress },
        { "localExposureFlatnessRisk", evidence.localExposureFlatnessRisk },
        { "localExposureDamageRisk", evidence.localExposureDamageRisk },
        { "subjectCenterPrior", evidence.subjectCenterPrior },
        { "subjectReadabilityPressure", evidence.subjectReadabilityPressure },
        { "subjectProtectionPressure", evidence.subjectProtectionPressure },
        { "subjectMoodPreservationPressure", evidence.subjectMoodPreservationPressure },
        { "subjectImportanceConfidence", evidence.subjectImportanceConfidence },
        { "smallSpecularLikely", evidence.smallSpecularLikely }
    };
}

float DevelopSubjectMapSmoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = SaturateFloat((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float DevelopSubjectMapRegionWeight(
    const EditorNodeGraph::DevelopSubjectImportanceRegion& region,
    float x,
    float y) {
    const float radiusX = std::clamp(region.radiusX, 0.005f, 1.0f);
    const float radiusY = std::clamp(region.radiusY, 0.005f, 1.0f);
    const float dx = (x - std::clamp(region.centerX, 0.0f, 1.0f)) / radiusX;
    const float dy = (y - std::clamp(region.centerY, 0.0f, 1.0f)) / radiusY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float feather = std::clamp(region.feather, 0.0f, 1.0f);
    const float inner = std::max(0.08f, 1.0f - feather * 0.55f);
    const float outer = 1.0f + feather * 0.35f;
    const float falloff = 1.0f - DevelopSubjectMapSmoothStep(inner, outer, distance);
    return SaturateFloat(region.strength * falloff);
}

float DevelopSubjectMapSegmentDistance(
    float px,
    float py,
    float ax,
    float ay,
    float bx,
    float by) {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float lenSq = vx * vx + vy * vy;
    if (lenSq <= 0.000001f) {
        const float dx = px - ax;
        const float dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / lenSq, 0.0f, 1.0f);
    const float sx = ax + vx * t;
    const float sy = ay + vy * t;
    const float dx = px - sx;
    const float dy = py - sy;
    return std::sqrt(dx * dx + dy * dy);
}

float DevelopSubjectMapStrokeWeight(
    const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke,
    float x,
    float y) {
    if (stroke.points.empty()) {
        return 0.0f;
    }

    float minDistance = std::numeric_limits<float>::max();
    if (stroke.points.size() == 1) {
        const float dx = x - std::clamp(stroke.points.front().x, 0.0f, 1.0f);
        const float dy = y - std::clamp(stroke.points.front().y, 0.0f, 1.0f);
        minDistance = std::sqrt(dx * dx + dy * dy);
    } else {
        for (std::size_t index = 1; index < stroke.points.size(); ++index) {
            const auto& a = stroke.points[index - 1];
            const auto& b = stroke.points[index];
            minDistance = std::min(
                minDistance,
                DevelopSubjectMapSegmentDistance(
                    x,
                    y,
                    std::clamp(a.x, 0.0f, 1.0f),
                    std::clamp(a.y, 0.0f, 1.0f),
                    std::clamp(b.x, 0.0f, 1.0f),
                    std::clamp(b.y, 0.0f, 1.0f)));
        }
    }

    const float radius = std::clamp(stroke.radius, 0.002f, 0.50f);
    const float feather = std::clamp(stroke.feather, 0.0f, 1.0f);
    const float inner = radius * std::max(0.20f, 1.0f - feather * 0.65f);
    const float outer = radius * (1.0f + feather * 0.85f);
    const float falloff = 1.0f - DevelopSubjectMapSmoothStep(inner, outer, minDistance);
    return SaturateFloat(stroke.strength * falloff);
}

void AddDevelopSubjectMapCellWeight(
    DevelopSubjectImportanceMapCell& cell,
    EditorNodeGraph::DevelopSubjectImportanceMode mode,
    float weight,
    bool lowPriority) {
    weight = SaturateFloat(weight);
    if (weight <= 0.001f) {
        return;
    }

    if (lowPriority || mode == EditorNodeGraph::DevelopSubjectImportanceMode::Ignore) {
        cell.lowPriority = std::max(cell.lowPriority, weight);
        return;
    }

    cell.importance = std::max(cell.importance, weight);
    switch (mode) {
        case EditorNodeGraph::DevelopSubjectImportanceMode::Reveal:
            cell.reveal = std::max(cell.reveal, weight);
            break;
        case EditorNodeGraph::DevelopSubjectImportanceMode::Protect:
            cell.protect = std::max(cell.protect, weight);
            break;
        case EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood:
            cell.preserveMood = std::max(cell.preserveMood, weight);
            break;
        case EditorNodeGraph::DevelopSubjectImportanceMode::Important:
        default:
            break;
    }
}

DevelopSubjectImportanceInterpretation InterpretDevelopSubjectImportanceMap(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    DevelopSubjectImportanceInterpretation result;
    result.enabled = importance.enabled;
    if (!importance.enabled) {
        return result;
    }

    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (region.enabled && region.strength > 0.001f) {
            ++result.activeRegionCount;
        }
    }
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (stroke.enabled && stroke.strength > 0.001f && !stroke.points.empty()) {
            ++result.activeStrokeCount;
        }
    }

    if (result.activeRegionCount == 0 && result.activeStrokeCount == 0) {
        result.status = "empty";
        result.reason = "Subject importance guidance is enabled, but no active regions or strokes are present.";
        return result;
    }

    result.active = true;
    result.status = "interpretedUserMarks";
    result.reason =
        "A compact solver grid was interpreted from user-marked subject regions and strokes; edge-aware refinement and visual diagnostic maps remain deferred.";

    for (int yIndex = 0; yIndex < kDevelopSubjectImportanceMapGridSize; ++yIndex) {
        for (int xIndex = 0; xIndex < kDevelopSubjectImportanceMapGridSize; ++xIndex) {
            const float x =
                (static_cast<float>(xIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            const float y =
                (static_cast<float>(yIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            DevelopSubjectImportanceMapCell& cell =
                result.cells[static_cast<std::size_t>(
                    yIndex * kDevelopSubjectImportanceMapGridSize + xIndex)];

            for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
                if (!region.enabled || region.strength <= 0.001f) {
                    continue;
                }
                AddDevelopSubjectMapCellWeight(
                    cell,
                    region.mode,
                    DevelopSubjectMapRegionWeight(region, x, y),
                    false);
            }
            for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
                if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
                    continue;
                }
                AddDevelopSubjectMapCellWeight(
                    cell,
                    stroke.mode,
                    DevelopSubjectMapStrokeWeight(stroke, x, y),
                    stroke.subtract);
            }

            if (cell.lowPriority > 0.001f) {
                cell.importance = std::max(0.0f, cell.importance * (1.0f - cell.lowPriority * 0.55f));
                cell.reveal = std::max(0.0f, cell.reveal * (1.0f - cell.lowPriority * 0.65f));
                cell.protect = std::max(0.0f, cell.protect * (1.0f - cell.lowPriority * 0.45f));
                cell.preserveMood =
                    std::max(0.0f, cell.preserveMood * (1.0f - cell.lowPriority * 0.35f));
            }
        }
    }

    float positiveCellCount = 0.0f;
    float lowPriorityCellCount = 0.0f;
    float revealCellCount = 0.0f;
    float protectCellCount = 0.0f;
    float moodCellCount = 0.0f;
    float positiveWeightSum = 0.0f;
    float centerWeightSum = 0.0f;
    float edgeWeightSum = 0.0f;
    for (int yIndex = 0; yIndex < kDevelopSubjectImportanceMapGridSize; ++yIndex) {
        for (int xIndex = 0; xIndex < kDevelopSubjectImportanceMapGridSize; ++xIndex) {
            const float x =
                (static_cast<float>(xIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            const float y =
                (static_cast<float>(yIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            const DevelopSubjectImportanceMapCell& cell =
                result.cells[static_cast<std::size_t>(
                    yIndex * kDevelopSubjectImportanceMapGridSize + xIndex)];
            const float positive = SaturateFloat(
                std::max({ cell.importance, cell.reveal, cell.protect, cell.preserveMood }));
            const float lowPriority = SaturateFloat(cell.lowPriority);
            if (positive > 0.025f) {
                positiveCellCount += 1.0f;
            }
            if (lowPriority > 0.025f) {
                lowPriorityCellCount += 1.0f;
            }
            if (cell.reveal > 0.025f) {
                revealCellCount += 1.0f;
            }
            if (cell.protect > 0.025f) {
                protectCellCount += 1.0f;
            }
            if (cell.preserveMood > 0.025f) {
                moodCellCount += 1.0f;
            }

            result.peakImportance = std::max(result.peakImportance, positive);
            positiveWeightSum += positive;
            const float dx = x - 0.5f;
            const float dy = y - 0.5f;
            const float edgeDistance = SaturateFloat(std::sqrt(dx * dx + dy * dy) / 0.707107f);
            centerWeightSum += positive * (1.0f - edgeDistance);
            edgeWeightSum += positive * edgeDistance;
        }
    }

    const float invCells = 1.0f / static_cast<float>(kDevelopSubjectImportanceMapCellCount);
    result.positiveCoverage = SaturateFloat(positiveCellCount * invCells);
    result.lowPriorityCoverage = SaturateFloat(lowPriorityCellCount * invCells);
    result.coverage = SaturateFloat(result.positiveCoverage + result.lowPriorityCoverage);
    result.revealCoverage = SaturateFloat(revealCellCount * invCells);
    result.protectCoverage = SaturateFloat(protectCellCount * invCells);
    result.moodCoverage = SaturateFloat(moodCellCount * invCells);
    result.meanImportance = SaturateFloat(positiveWeightSum * invCells);
    result.centerBias = positiveWeightSum > 0.001f
        ? SaturateFloat(centerWeightSum / positiveWeightSum)
        : 0.0f;
    result.edgeBias = positiveWeightSum > 0.001f
        ? SaturateFloat(edgeWeightSum / positiveWeightSum)
        : 0.0f;
    result.mapConfidence = SaturateFloat(
        result.peakImportance * 0.30f +
        result.positiveCoverage * 0.30f +
        result.lowPriorityCoverage * 0.16f +
        result.meanImportance * 0.16f +
        (result.active ? 0.08f : 0.0f));
    if (result.positiveCoverage <= 0.001f && result.lowPriorityCoverage > 0.001f) {
        result.reason =
            "Only low-priority subject marks are active; Auto can spend less exposure/detail budget there while preserving scene intent.";
    }
    return result;
}

nlohmann::json DevelopSubjectImportanceInterpretationToJson(
    const DevelopSubjectImportanceInterpretation& map) {
    nlohmann::json cells = nlohmann::json::array();
    for (int yIndex = 0; yIndex < map.gridHeight; ++yIndex) {
        for (int xIndex = 0; xIndex < map.gridWidth; ++xIndex) {
            const DevelopSubjectImportanceMapCell& cell =
                map.cells[static_cast<std::size_t>(yIndex * map.gridWidth + xIndex)];
            cells.push_back({
                { "x", xIndex },
                { "y", yIndex },
                { "importance", cell.importance },
                { "reveal", cell.reveal },
                { "protect", cell.protect },
                { "preserveMood", cell.preserveMood },
                { "lowPriority", cell.lowPriority }
            });
        }
    }

    return {
        { "version", kDevelopSubjectImportanceMapVersion },
        { "enabled", map.enabled },
        { "active", map.active },
        { "status", map.status },
        { "reason", map.reason },
        { "gridWidth", map.gridWidth },
        { "gridHeight", map.gridHeight },
        { "activeRegionCount", map.activeRegionCount },
        { "activeStrokeCount", map.activeStrokeCount },
        { "coverage", map.coverage },
        { "positiveCoverage", map.positiveCoverage },
        { "lowPriorityCoverage", map.lowPriorityCoverage },
        { "revealCoverage", map.revealCoverage },
        { "protectCoverage", map.protectCoverage },
        { "moodCoverage", map.moodCoverage },
        { "peakImportance", map.peakImportance },
        { "meanImportance", map.meanImportance },
        { "centerBias", map.centerBias },
        { "edgeBias", map.edgeBias },
        { "mapConfidence", map.mapConfidence },
        { "cells", std::move(cells) }
    };
}

float DevelopSubjectImportanceCellPositive(const DevelopSubjectImportanceMapCell& cell) {
    return SaturateFloat(std::max({ cell.importance, cell.reveal, cell.protect, cell.preserveMood }));
}

DevelopSubjectRefinedMap BuildDevelopSubjectRefinedMap(
    const DevelopSubjectImportanceInterpretation& sourceMap,
    const DevelopSubjectSceneIntent& subjectIntent) {
    DevelopSubjectRefinedMap refined;
    refined.enabled = sourceMap.enabled;
    refined.gridWidth = sourceMap.gridWidth;
    refined.gridHeight = sourceMap.gridHeight;
    if (!sourceMap.enabled) {
        return refined;
    }
    if (!sourceMap.active) {
        refined.status = sourceMap.status;
        refined.reason = sourceMap.reason;
        return refined;
    }

    refined.active = true;
    refined.status = "refinedUserMarks";
    refined.reason =
        "SubjectRefinedMapV1 blends the compact interpreted user marks with neighbor support and solved subject/readability/protection/mood axes. Boundary hints are mark-structure hints; true image-edge refinement remains deferred.";

    const int width = std::max(1, sourceMap.gridWidth);
    const int height = std::max(1, sourceMap.gridHeight);
    auto cellAt = [&](int x, int y) -> const DevelopSubjectImportanceMapCell& {
        const int clampedX = std::clamp(x, 0, width - 1);
        const int clampedY = std::clamp(y, 0, height - 1);
        return sourceMap.cells[static_cast<std::size_t>(clampedY * width + clampedX)];
    };

    const float subjectPressure = SaturateFloat(
        subjectIntent.subjectPriority * 0.42f +
        subjectIntent.userGuidanceStrength * 0.22f +
        sourceMap.mapConfidence * 0.20f -
        subjectIntent.sceneIntegrity * 0.10f);
    const float readabilityPressure = SaturateFloat(
        subjectIntent.improveReadability * 0.42f +
        subjectIntent.readabilityPressure * 0.22f +
        sourceMap.revealCoverage * 0.16f -
        subjectIntent.preserveMood * 0.08f);
    const float protectionPressure = SaturateFloat(
        subjectIntent.protectionPressure * 0.42f +
        sourceMap.protectCoverage * 0.22f +
        subjectIntent.importanceProtection * 0.18f);
    const float moodPressure = SaturateFloat(
        subjectIntent.preserveMood * 0.42f +
        subjectIntent.moodPreservationPressure * 0.22f +
        sourceMap.moodCoverage * 0.18f);

    float coverageCells = 0.0f;
    float lowPriorityCells = 0.0f;
    float readabilityCells = 0.0f;
    float protectionCells = 0.0f;
    float moodCells = 0.0f;
    float confidenceSum = 0.0f;
    float boundarySum = 0.0f;

    for (int yIndex = 0; yIndex < height; ++yIndex) {
        for (int xIndex = 0; xIndex < width; ++xIndex) {
            const std::size_t index = static_cast<std::size_t>(yIndex * width + xIndex);
            const DevelopSubjectImportanceMapCell& sourceCell = sourceMap.cells[index];
            const float positive = DevelopSubjectImportanceCellPositive(sourceCell);
            const float lowPriority = SaturateFloat(sourceCell.lowPriority);

            float neighborPositiveSum = 0.0f;
            float neighborLowSum = 0.0f;
            float neighborCount = 0.0f;
            float boundaryHint = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const DevelopSubjectImportanceMapCell& neighbor = cellAt(xIndex + dx, yIndex + dy);
                    const float neighborPositive = DevelopSubjectImportanceCellPositive(neighbor);
                    const float neighborLow = SaturateFloat(neighbor.lowPriority);
                    neighborPositiveSum += neighborPositive;
                    neighborLowSum += neighborLow;
                    neighborCount += 1.0f;
                    boundaryHint = std::max(
                        boundaryHint,
                        std::max(
                            std::fabs(positive - neighborPositive),
                            std::fabs(lowPriority - neighborLow)));
                }
            }
            const float neighborPositive =
                neighborCount > 0.0f ? SaturateFloat(neighborPositiveSum / neighborCount) : 0.0f;
            const float neighborLow =
                neighborCount > 0.0f ? SaturateFloat(neighborLowSum / neighborCount) : 0.0f;
            boundaryHint = SaturateFloat(boundaryHint * 0.72f + std::fabs(positive - lowPriority) * 0.20f);

            DevelopSubjectRefinedMapCell& refinedCell = refined.cells[index];
            refinedCell.lowPriority = SaturateFloat(lowPriority * 0.82f + neighborLow * 0.12f);
            refinedCell.importance = SaturateFloat(
                positive * (0.66f + subjectPressure * 0.20f) +
                neighborPositive * 0.18f -
                refinedCell.lowPriority * 0.22f);
            refinedCell.readability = SaturateFloat(
                sourceCell.reveal * 0.64f +
                refinedCell.importance * 0.20f +
                readabilityPressure * 0.16f -
                refinedCell.lowPriority * 0.16f);
            refinedCell.protection = SaturateFloat(
                sourceCell.protect * 0.66f +
                refinedCell.importance * 0.16f +
                protectionPressure * 0.16f -
                refinedCell.lowPriority * 0.10f);
            refinedCell.preserveMood = SaturateFloat(
                sourceCell.preserveMood * 0.66f +
                refinedCell.importance * 0.14f +
                moodPressure * 0.18f +
                refinedCell.lowPriority * 0.04f -
                readabilityPressure * 0.06f);
            refinedCell.boundaryHint = boundaryHint;
            refinedCell.confidence = SaturateFloat(
                refinedCell.importance * 0.34f +
                neighborPositive * 0.20f +
                sourceMap.mapConfidence * 0.20f +
                subjectIntent.userGuidanceStrength * 0.12f +
                boundaryHint * 0.06f -
                refinedCell.lowPriority * 0.10f);

            if (refinedCell.importance > 0.025f || refinedCell.confidence > 0.08f) {
                coverageCells += 1.0f;
            }
            if (refinedCell.lowPriority > 0.025f) {
                lowPriorityCells += 1.0f;
            }
            if (refinedCell.readability > 0.025f) {
                readabilityCells += 1.0f;
            }
            if (refinedCell.protection > 0.025f) {
                protectionCells += 1.0f;
            }
            if (refinedCell.preserveMood > 0.025f) {
                moodCells += 1.0f;
            }
            refined.peakImportance = std::max(refined.peakImportance, refinedCell.importance);
            confidenceSum += refinedCell.confidence;
            boundarySum += refinedCell.boundaryHint * std::max(refinedCell.importance, refinedCell.confidence);
        }
    }

    const float invCells = 1.0f / static_cast<float>(kDevelopSubjectImportanceMapCellCount);
    refined.coverage = SaturateFloat(coverageCells * invCells);
    refined.lowPriorityCoverage = SaturateFloat(lowPriorityCells * invCells);
    refined.readabilityCoverage = SaturateFloat(readabilityCells * invCells);
    refined.protectionCoverage = SaturateFloat(protectionCells * invCells);
    refined.moodCoverage = SaturateFloat(moodCells * invCells);
    refined.meanConfidence = SaturateFloat(confidenceSum * invCells);
    refined.boundaryHint = SaturateFloat(boundarySum * invCells);
    if (refined.coverage <= 0.001f && refined.lowPriorityCoverage > 0.001f) {
        refined.reason =
            "SubjectRefinedMapV1 only found low-priority guidance, so it can reduce budget pressure without creating a positive subject target.";
    }
    return refined;
}

nlohmann::json DevelopSubjectRefinedMapToJson(const DevelopSubjectRefinedMap& map) {
    nlohmann::json cells = nlohmann::json::array();
    for (int yIndex = 0; yIndex < map.gridHeight; ++yIndex) {
        for (int xIndex = 0; xIndex < map.gridWidth; ++xIndex) {
            const DevelopSubjectRefinedMapCell& cell =
                map.cells[static_cast<std::size_t>(yIndex * map.gridWidth + xIndex)];
            cells.push_back({
                { "x", xIndex },
                { "y", yIndex },
                { "importance", cell.importance },
                { "confidence", cell.confidence },
                { "readability", cell.readability },
                { "protection", cell.protection },
                { "preserveMood", cell.preserveMood },
                { "lowPriority", cell.lowPriority },
                { "boundaryHint", cell.boundaryHint }
            });
        }
    }

    return {
        { "version", kDevelopSubjectRefinedMapVersion },
        { "sourceMapVersion", map.sourceMapVersion },
        { "enabled", map.enabled },
        { "active", map.active },
        { "status", map.status },
        { "reason", map.reason },
        { "gridWidth", map.gridWidth },
        { "gridHeight", map.gridHeight },
        { "coverage", map.coverage },
        { "lowPriorityCoverage", map.lowPriorityCoverage },
        { "readabilityCoverage", map.readabilityCoverage },
        { "protectionCoverage", map.protectionCoverage },
        { "moodCoverage", map.moodCoverage },
        { "peakImportance", map.peakImportance },
        { "confidence", map.meanConfidence },
        { "boundaryHint", map.boundaryHint },
        { "cells", std::move(cells) }
    };
}

void ApplyDevelopSubjectRefinedMap(
    DevelopSubjectSceneIntent& subjectIntent,
    const DevelopSubjectImportanceInterpretation& importanceMap) {
    const DevelopSubjectRefinedMap refinedMap =
        BuildDevelopSubjectRefinedMap(importanceMap, subjectIntent);
    subjectIntent.refinedImportanceMap = DevelopSubjectRefinedMapToJson(refinedMap);
    subjectIntent.refinedMapCoverage = refinedMap.coverage;
    subjectIntent.refinedMapLowPriorityCoverage = refinedMap.lowPriorityCoverage;
    subjectIntent.refinedMapReadabilityCoverage = refinedMap.readabilityCoverage;
    subjectIntent.refinedMapProtectionCoverage = refinedMap.protectionCoverage;
    subjectIntent.refinedMapMoodCoverage = refinedMap.moodCoverage;
    subjectIntent.refinedMapPeak = refinedMap.peakImportance;
    subjectIntent.refinedMapConfidence = refinedMap.meanConfidence;
    subjectIntent.refinedMapBoundaryHint = refinedMap.boundaryHint;
}

DevelopSubjectImportanceSummary SummarizeDevelopSubjectImportance(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    DevelopSubjectImportanceSummary summary;
    summary.enabled = importance.enabled;
    if (!importance.enabled) {
        return summary;
    }

    auto addModeWeight = [&](EditorNodeGraph::DevelopSubjectImportanceMode mode, float weight) {
        switch (mode) {
            case EditorNodeGraph::DevelopSubjectImportanceMode::Reveal:
                summary.reveal = SaturateFloat(summary.reveal + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::Protect:
                summary.protect = SaturateFloat(summary.protect + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood:
                summary.preserveMood = SaturateFloat(summary.preserveMood + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::Ignore:
                summary.ignore = SaturateFloat(summary.ignore + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::Important:
            default:
                summary.important = SaturateFloat(summary.important + weight);
                break;
        }
    };

    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (!region.enabled || region.strength <= 0.001f) {
            continue;
        }
        const float softArea = std::clamp(
            std::sqrt(std::max(0.0001f, region.radiusX * region.radiusY)) * 2.2f,
            0.08f,
            1.0f);
        const float featherSupport = 0.72f + std::clamp(region.feather, 0.0f, 1.0f) * 0.28f;
        const float weight = SaturateFloat(region.strength * softArea * featherSupport);
        if (weight <= 0.001f) {
            continue;
        }
        ++summary.activeRegionCount;
        summary.strength = SaturateFloat(summary.strength + weight);
        addModeWeight(region.mode, weight);
    }

    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
            continue;
        }

        float strokeLength = 0.0f;
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            const float dx = stroke.points[i].x - stroke.points[i - 1].x;
            const float dy = stroke.points[i].y - stroke.points[i - 1].y;
            strokeLength += std::sqrt(dx * dx + dy * dy);
        }
        const float pointSupport =
            static_cast<float>(std::min<std::size_t>(stroke.points.size(), 64)) * stroke.radius * 0.055f;
        const float pathSupport = strokeLength * stroke.radius * 1.65f;
        const float softCoverage = std::clamp(pointSupport + pathSupport, 0.025f, 0.85f);
        const float featherSupport = 0.70f + std::clamp(stroke.feather, 0.0f, 1.0f) * 0.30f;
        const float weight = SaturateFloat(stroke.strength * softCoverage * featherSupport);
        if (weight <= 0.001f) {
            continue;
        }

        ++summary.activeStrokeCount;
        if (stroke.subtract) {
            summary.ignore = SaturateFloat(summary.ignore + weight * 0.70f);
            summary.strength = std::max(0.0f, summary.strength - weight * 0.35f);
            continue;
        }
        summary.strength = SaturateFloat(summary.strength + weight);
        addModeWeight(stroke.mode, weight);
    }

    summary.subjectPriority = SaturateFloat(
        summary.important * 0.52f +
        summary.reveal * 0.42f +
        summary.protect * 0.28f -
        summary.ignore * 0.34f);
    summary.readability = SaturateFloat(
        summary.reveal * 0.62f +
        summary.important * 0.26f -
        summary.preserveMood * 0.22f -
        summary.ignore * 0.18f);
    summary.protection = SaturateFloat(
        summary.protect * 0.68f +
        summary.important * 0.18f +
        summary.preserveMood * 0.12f);
    summary.mood = SaturateFloat(
        summary.preserveMood * 0.72f +
        summary.protect * 0.12f -
        summary.reveal * 0.20f);
    summary.lowPriority = SaturateFloat(summary.ignore);
    return summary;
}

void AddDevelopSubjectSolveNote(
    nlohmann::json& notes,
    const char* id,
    const std::string& text,
    float strength) {
    if (!notes.is_array() || notes.size() >= 5 || text.empty() || strength <= 0.001f) {
        return;
    }
    notes.push_back({
        { "id", id },
        { "text", text },
        { "strength", SaturateFloat(strength) }
    });
}

nlohmann::json BuildDevelopSubjectSolveNotes(const DevelopSubjectSceneIntent& subjectIntent) {
    nlohmann::json notes = nlohmann::json::array();
    const bool hasMarks =
        subjectIntent.importanceRegionCount > 0 ||
        subjectIntent.importanceStrokeCount > 0;
    const int interpretedCells = static_cast<int>(
        std::round(subjectIntent.importanceMapCoverage *
            static_cast<float>(kDevelopSubjectImportanceMapCellCount)));

    if (hasMarks && subjectIntent.importanceMapConfidence > 0.01f) {
        char buffer[256];
        std::snprintf(
            buffer,
            sizeof(buffer),
            "Interpreted %d subject-map cell%s from marked regions/strokes; Auto treats them as soft scoring bias, not a hard mask.",
            std::max(1, interpretedCells),
            std::max(1, interpretedCells) == 1 ? "" : "s");
        AddDevelopSubjectSolveNote(
            notes,
            "interpretedMapBias",
            buffer,
            subjectIntent.importanceMapConfidence);
        if (subjectIntent.refinedMapConfidence > 0.08f) {
            AddDevelopSubjectSolveNote(
                notes,
                "refinedMapBias",
                "Refined subject-map confidence is guiding readability, protection, and mood tradeoffs while true image-edge refinement remains deferred.",
                subjectIntent.refinedMapConfidence);
        }
    } else if (subjectIntent.userGuidanceActive && !hasMarks) {
        AddDevelopSubjectSolveNote(
            notes,
            "intentControlBias",
            "Subject / Scene and Mood / Readability controls are biasing candidate scores without a painted subject map.",
            subjectIntent.userGuidanceStrength);
    } else if (subjectIntent.automaticOnly && subjectIntent.automaticConfidence > 0.08f) {
        AddDevelopSubjectSolveNote(
            notes,
            "automaticWeakPrior",
            "Auto found only weak automatic subject evidence, so subject priority remains a conservative scene-level bias.",
            subjectIntent.automaticConfidence);
    }

    if (subjectIntent.importanceReveal > 0.12f ||
        subjectIntent.importanceMapRevealCoverage > 0.02f ||
        subjectIntent.improveReadability > subjectIntent.preserveMood + 0.16f) {
        AddDevelopSubjectSolveNote(
            notes,
            "readabilityBias",
            "Reveal/readability evidence raises subject-readable midtone and local-range candidate fit while preserving noise and halo guards.",
            std::max({
                subjectIntent.importanceReveal,
                subjectIntent.importanceMapRevealCoverage,
                std::max(0.0f, subjectIntent.improveReadability - subjectIntent.preserveMood) }));
    }

    if (subjectIntent.importanceProtect > 0.12f ||
        subjectIntent.importanceMapProtectCoverage > 0.02f ||
        subjectIntent.protectionPressure > 0.18f) {
        AddDevelopSubjectSolveNote(
            notes,
            "protectionBias",
            "Protect evidence raises clipping, detail, and over-compression safeguards for marked or likely important content.",
            std::max({
                subjectIntent.importanceProtect,
                subjectIntent.importanceMapProtectCoverage,
                subjectIntent.protectionPressure }));
    }

    if (subjectIntent.importancePreserveMood > 0.12f ||
        subjectIntent.importanceMapMoodCoverage > 0.02f ||
        subjectIntent.preserveMood > subjectIntent.improveReadability + 0.16f) {
        AddDevelopSubjectSolveNote(
            notes,
            "moodPreservationBias",
            "Mood-preserve evidence pushes back on forcing low-key or atmosphere-critical areas into gray mids.",
            std::max({
                subjectIntent.importancePreserveMood,
                subjectIntent.importanceMapMoodCoverage,
                std::max(0.0f, subjectIntent.preserveMood - subjectIntent.improveReadability) }));
    }

    if (subjectIntent.importanceIgnore > 0.12f ||
        subjectIntent.importanceMapLowPriorityCoverage > 0.02f ||
        subjectIntent.importanceLowPriority > 0.12f) {
        AddDevelopSubjectSolveNote(
            notes,
            "lowPriorityBias",
            "Low-priority marks reduce exposure/detail budget pressure in those areas instead of making the whole image brighter.",
            std::max({
                subjectIntent.importanceIgnore,
                subjectIntent.importanceMapLowPriorityCoverage,
                subjectIntent.importanceLowPriority }));
    }

    if (subjectIntent.subjectSceneAxis > 0.18f) {
        AddDevelopSubjectSolveNote(
            notes,
            "subjectPriorityAxis",
            "The solved Subject / Scene axis leans toward marked or likely subject priority.",
            subjectIntent.subjectSceneAxis);
    } else if (subjectIntent.subjectSceneAxis < -0.18f) {
        AddDevelopSubjectSolveNote(
            notes,
            "sceneIntegrityAxis",
            "The solved Subject / Scene axis leans toward global scene integrity, so subject evidence stays restrained.",
            -subjectIntent.subjectSceneAxis);
    }

    if (notes.empty() && !subjectIntent.reason.empty()) {
        AddDevelopSubjectSolveNote(notes, "intentReason", subjectIntent.reason, 0.25f);
    }
    return notes;
}

DevelopSubjectSceneIntent ResolveDevelopSubjectSceneIntent(
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence) {
    DevelopSubjectSceneIntent subjectIntent;
    subjectIntent.evidence = DevelopDynamicRangeRegionEvidenceToJson(regionEvidence);
    const DevelopSubjectImportanceSummary importanceSummary =
        SummarizeDevelopSubjectImportance(importance);
    const DevelopSubjectImportanceInterpretation importanceMap =
        InterpretDevelopSubjectImportanceMap(importance);
    subjectIntent.importanceMap = DevelopSubjectImportanceInterpretationToJson(importanceMap);
    subjectIntent.importanceRegionCount = importanceSummary.activeRegionCount;
    subjectIntent.importanceStrokeCount = importanceSummary.activeStrokeCount;
    subjectIntent.importanceStrength = importanceSummary.strength;
    subjectIntent.importanceImportant = importanceSummary.important;
    subjectIntent.importanceReveal = importanceSummary.reveal;
    subjectIntent.importanceProtect = importanceSummary.protect;
    subjectIntent.importancePreserveMood = importanceSummary.preserveMood;
    subjectIntent.importanceIgnore = importanceSummary.ignore;
    subjectIntent.importanceSubjectPriority = importanceSummary.subjectPriority;
    subjectIntent.importanceReadability = importanceSummary.readability;
    subjectIntent.importanceProtection = importanceSummary.protection;
    subjectIntent.importanceMood = importanceSummary.mood;
    subjectIntent.importanceLowPriority = importanceSummary.lowPriority;
    subjectIntent.importanceMapCoverage = importanceMap.coverage;
    subjectIntent.importanceMapPositiveCoverage = importanceMap.positiveCoverage;
    subjectIntent.importanceMapLowPriorityCoverage = importanceMap.lowPriorityCoverage;
    subjectIntent.importanceMapRevealCoverage = importanceMap.revealCoverage;
    subjectIntent.importanceMapProtectCoverage = importanceMap.protectCoverage;
    subjectIntent.importanceMapMoodCoverage = importanceMap.moodCoverage;
    subjectIntent.importanceMapPeak = importanceMap.peakImportance;
    subjectIntent.importanceMapConfidence = importanceMap.mapConfidence;
    subjectIntent.importanceMapCenterBias = importanceMap.centerBias;
    subjectIntent.importanceMapEdgeBias = importanceMap.edgeBias;
    subjectIntent.userSubjectSceneBias = std::clamp(guidance.subjectSceneBias, -1.0f, 1.0f);
    subjectIntent.userMoodReadabilityBias = std::clamp(guidance.moodReadabilityBias, -1.0f, 1.0f);
    subjectIntent.userGuidanceStrength = std::max(
        std::fabs(subjectIntent.userSubjectSceneBias),
        std::fabs(subjectIntent.userMoodReadabilityBias));
    subjectIntent.userGuidanceStrength = std::max(
        subjectIntent.userGuidanceStrength,
        std::max(importanceSummary.strength, importanceSummary.lowPriority));
    subjectIntent.userGuidanceStrength = std::max(
        subjectIntent.userGuidanceStrength,
        importanceMap.mapConfidence * 0.72f);
    const bool userIntentActive = subjectIntent.userGuidanceStrength > 0.015f;
    auto applyUserSubjectIntent = [&]() {
        if (!userIntentActive) {
            return;
        }
        const float subjectBias = std::max(0.0f, subjectIntent.userSubjectSceneBias);
        const float sceneBias = std::max(0.0f, -subjectIntent.userSubjectSceneBias);
        const float readabilityBias = std::max(0.0f, subjectIntent.userMoodReadabilityBias);
        const float moodBias = std::max(0.0f, -subjectIntent.userMoodReadabilityBias);

        // User axes and marked regions are stronger than the weak automatic
        // prior, but clipping/noise/halo safeguards still decide viability.
        subjectIntent.userGuidanceStatus = importanceSummary.activeStrokeCount > 0
            ? "importanceBrush"
            : (importanceSummary.activeRegionCount > 0 ? "importanceRegions" : "intentControls");
        subjectIntent.userGuidanceActive = true;
        subjectIntent.automaticOnly = false;
        subjectIntent.subjectPriority = SaturateFloat(
            subjectIntent.subjectPriority +
            subjectBias * 0.34f +
            readabilityBias * 0.06f +
            importanceSummary.subjectPriority * 0.34f +
            importanceSummary.reveal * 0.08f -
            importanceMap.lowPriorityCoverage * 0.12f +
            importanceMap.mapConfidence * 0.08f +
            importanceMap.centerBias * importanceMap.positiveCoverage * 0.06f -
            sceneBias * 0.20f -
            importanceSummary.lowPriority * 0.20f);
        subjectIntent.sceneIntegrity = SaturateFloat(
            subjectIntent.sceneIntegrity +
            sceneBias * 0.34f +
            moodBias * 0.06f +
            importanceSummary.lowPriority * 0.22f +
            importanceSummary.mood * 0.10f +
            importanceMap.lowPriorityCoverage * 0.10f +
            importanceMap.edgeBias * importanceMap.positiveCoverage * 0.04f -
            subjectBias * 0.08f -
            importanceSummary.reveal * 0.08f);
        subjectIntent.improveReadability = SaturateFloat(
            subjectIntent.improveReadability +
            readabilityBias * 0.34f +
            subjectBias * 0.06f +
            importanceSummary.readability * 0.38f +
            importanceMap.revealCoverage * 0.10f +
            importanceMap.mapConfidence * 0.04f -
            moodBias * 0.16f -
            importanceSummary.mood * 0.18f -
            importanceSummary.lowPriority * 0.10f);
        subjectIntent.preserveMood = SaturateFloat(
            subjectIntent.preserveMood +
            moodBias * 0.34f +
            sceneBias * 0.08f +
            importanceSummary.mood * 0.40f +
            importanceSummary.protection * 0.08f +
            importanceMap.moodCoverage * 0.10f +
            importanceMap.lowPriorityCoverage * 0.05f -
            readabilityBias * 0.12f -
            importanceSummary.reveal * 0.14f);
        subjectIntent.protectionPressure = SaturateFloat(
            subjectIntent.protectionPressure +
            importanceSummary.protection * 0.34f +
            importanceMap.protectCoverage * 0.08f);
    };
    if (!stats.valid) {
        subjectIntent.id = "pendingSubjectEvidence";
        subjectIntent.label = "Pending Subject Evidence";
        subjectIntent.reason =
            "Develop needs rendered statistics before it can estimate subject or scene priority.";
        applyUserSubjectIntent();
        if (userIntentActive) {
            subjectIntent.id = "userGuidedPendingSubjectEvidence";
            subjectIntent.label = "User Guided Pending Evidence";
            subjectIntent.reason =
                "User subject/scene intent is active, but Develop still needs rendered statistics before it can validate the tradeoff.";
        }
        ApplyDevelopSubjectRefinedMap(subjectIntent, importanceMap);
        subjectIntent.solveNotes = BuildDevelopSubjectSolveNotes(subjectIntent);
        return subjectIntent;
    }

    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;

    subjectIntent.centerPrior = SaturateFloat(regionEvidence.subjectCenterPrior);
    subjectIntent.readabilityPressure = SaturateFloat(regionEvidence.subjectReadabilityPressure);
    subjectIntent.protectionPressure = SaturateFloat(regionEvidence.subjectProtectionPressure);
    subjectIntent.moodPreservationPressure =
        SaturateFloat(regionEvidence.subjectMoodPreservationPressure);
    subjectIntent.automaticConfidence =
        SaturateFloat(regionEvidence.subjectImportanceConfidence);

    subjectIntent.subjectPriority = SaturateFloat(
        0.42f +
        subjectIntent.automaticConfidence * 0.28f +
        subjectIntent.readabilityPressure * 0.16f +
        subjectIntent.protectionPressure * 0.12f +
        std::max(0.0f, guidance.shadowLift) * 0.04f +
        (brightIntent ? 0.07f : 0.0f) +
        (flatIntent ? 0.05f : 0.0f) +
        (rangeIntent ? 0.04f : 0.0f) -
        subjectIntent.moodPreservationPressure * 0.08f -
        (darkIntent ? 0.04f : 0.0f));
    subjectIntent.sceneIntegrity = SaturateFloat(
        0.52f +
        subjectIntent.moodPreservationPressure * 0.22f +
        regionEvidence.localExposureDamageRisk * 0.10f +
        regionEvidence.localHaloRisk * 0.08f +
        (darkIntent ? 0.08f : 0.0f) +
        (punchyIntent ? 0.05f : 0.0f) -
        subjectIntent.readabilityPressure * 0.08f -
        (brightIntent ? 0.05f : 0.0f));
    subjectIntent.improveReadability = SaturateFloat(
        0.42f +
        subjectIntent.readabilityPressure * 0.34f +
        regionEvidence.localShadowHotspotRisk * 0.12f +
        std::max(0.0f, guidance.shadowLift) * 0.08f +
        (brightIntent ? 0.10f : 0.0f) +
        (flatIntent ? 0.08f : 0.0f) +
        (rangeIntent ? 0.05f : 0.0f) -
        regionEvidence.shadowNoiseLiftRisk * 0.14f -
        subjectIntent.moodPreservationPressure * 0.10f -
        (darkIntent ? 0.06f : 0.0f));
    subjectIntent.preserveMood = SaturateFloat(
        0.45f +
        subjectIntent.moodPreservationPressure * 0.34f +
        regionEvidence.shadowNoiseLiftRisk * 0.10f +
        regionEvidence.localExposureHaloStress * 0.06f +
        (darkIntent ? 0.14f : 0.0f) +
        (punchyIntent ? 0.06f : 0.0f) -
        subjectIntent.readabilityPressure * 0.12f -
        (brightIntent ? 0.08f : 0.0f));
    subjectIntent.subjectSceneAxis =
        std::clamp(subjectIntent.subjectPriority - subjectIntent.sceneIntegrity, -1.0f, 1.0f);
    subjectIntent.moodReadabilityAxis =
        std::clamp(subjectIntent.improveReadability - subjectIntent.preserveMood, -1.0f, 1.0f);

    applyUserSubjectIntent();
    subjectIntent.subjectSceneAxis =
        std::clamp(subjectIntent.subjectPriority - subjectIntent.sceneIntegrity, -1.0f, 1.0f);
    subjectIntent.moodReadabilityAxis =
        std::clamp(subjectIntent.improveReadability - subjectIntent.preserveMood, -1.0f, 1.0f);

    if (userIntentActive) {
        subjectIntent.id = "userGuidedSubjectSceneIntent";
        subjectIntent.label = "User Guided Subject / Scene";
        const bool markedBrushActive = importanceSummary.activeStrokeCount > 0;
        const bool markedRegionActive = importanceSummary.activeRegionCount > 0;
        const char* markedNoun = markedBrushActive ? "brush strokes" : "regions";
        if ((markedBrushActive || markedRegionActive) && importanceSummary.reveal > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushReveal" : "importanceRegionReveal";
            subjectIntent.label = markedBrushActive ? "Painted Reveal" : "Marked Region Reveal";
            subjectIntent.reason =
                std::string("Marked reveal/important ") + markedNoun + " are active, so Auto scores subject-readable local range and midtone candidates higher while keeping quality guards.";
        } else if ((markedBrushActive || markedRegionActive) && importanceSummary.protect > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushProtect" : "importanceRegionProtect";
            subjectIntent.label = markedBrushActive ? "Painted Protection" : "Marked Region Protection";
            subjectIntent.reason =
                std::string("Marked protection ") + markedNoun + " are active, so Auto scores clipping, detail, and over-compression safeguards higher for important content.";
        } else if ((markedBrushActive || markedRegionActive) && importanceSummary.preserveMood > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushMood" : "importanceRegionMood";
            subjectIntent.label = markedBrushActive ? "Painted Mood Preservation" : "Marked Mood Preservation";
            subjectIntent.reason =
                std::string("Marked mood-preserve ") + markedNoun + " are active, so Auto avoids forcing low-key or atmosphere-critical regions into neutral mids.";
        } else if ((markedBrushActive || markedRegionActive) && importanceSummary.ignore > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushLowPriority" : "importanceRegionLowPriority";
            subjectIntent.label = markedBrushActive ? "Painted Low Priority" : "Marked Low Priority";
            subjectIntent.reason =
                std::string("Low-priority ") + markedNoun + " are active, so Auto spends less exposure and cleanup budget there while protecting the overall scene.";
        } else if (markedBrushActive || markedRegionActive) {
            subjectIntent.id = markedBrushActive ? "importanceBrushGuidance" : "importanceRegionGuidance";
            subjectIntent.label = markedBrushActive ? "Painted Important Areas" : "Marked Important Regions";
            subjectIntent.reason =
                std::string("Marked important ") + markedNoun + " are biasing Auto's candidate scores without becoming hard-edged masks.";
        } else if (subjectIntent.userSubjectSceneBias > 0.30f &&
            subjectIntent.userMoodReadabilityBias > 0.20f) {
            subjectIntent.label = "User Guided Subject Readability";
            subjectIntent.reason =
                "Subject priority and readability intent are active, so Auto scores subject-readable local range and midtone candidates higher while keeping safety guards.";
        } else if (subjectIntent.userSubjectSceneBias > 0.30f &&
            subjectIntent.userMoodReadabilityBias < -0.20f) {
            subjectIntent.label = "User Guided Protected Mood";
            subjectIntent.reason =
                "Subject priority is active while mood preservation is favored, so Auto protects the marked/likely subject without forcing a low-key scene into gray mids.";
        } else if (subjectIntent.userSubjectSceneBias < -0.30f) {
            subjectIntent.label = "User Guided Scene Integrity";
            subjectIntent.reason =
                "Global scene integrity is favored, so Auto keeps subject evidence as a softer bias and avoids spending all range on the likely subject.";
        } else if (subjectIntent.userMoodReadabilityBias > 0.25f) {
            subjectIntent.label = "User Guided Readability";
            subjectIntent.reason =
                "Readability intent is active, so Auto scores useful shadow/midtone candidates higher when quality allows.";
        } else if (subjectIntent.userMoodReadabilityBias < -0.25f) {
            subjectIntent.label = "User Guided Mood Preservation";
            subjectIntent.reason =
                "Mood preservation intent is active, so Auto avoids unnecessary subject lift and protects low-key atmosphere.";
        } else {
            subjectIntent.reason =
                "User subject/scene intent controls are active and are biasing Auto's candidate scores without creating a hard mask.";
        }
    } else if (!regionEvidence.valid) {
        subjectIntent.id = "awaitingRenderedSubjectEvidence";
        subjectIntent.label = "Awaiting Subject Evidence";
        subjectIntent.reason =
            "Auto has no rendered candidate metrics yet, so subject priority remains a neutral weak prior.";
    } else if (subjectIntent.readabilityPressure > 0.42f &&
        subjectIntent.automaticConfidence > 0.34f &&
        subjectIntent.preserveMood < subjectIntent.improveReadability + 0.12f) {
        subjectIntent.id = "automaticReadabilityBias";
        subjectIntent.label = "Automatic Readability Bias";
        subjectIntent.reason =
            "A likely important central region is dark enough that Auto should score readable-shadow and local-range candidates a little higher.";
    } else if (subjectIntent.protectionPressure > 0.38f &&
        subjectIntent.automaticConfidence > 0.32f) {
        subjectIntent.id = "automaticProtectionBias";
        subjectIntent.label = "Automatic Protection Bias";
        subjectIntent.reason =
            "A likely important central or structured region has highlight/protection pressure, so Auto should avoid sacrificing it to global range moves.";
    } else if (subjectIntent.moodPreservationPressure > 0.38f ||
        subjectIntent.preserveMood > subjectIntent.improveReadability + 0.16f) {
        subjectIntent.id = "automaticMoodPreservationBias";
        subjectIntent.label = "Automatic Mood Preservation Bias";
        subjectIntent.reason =
            "The weak subject prior looks more like low-key or silhouette intent, so Auto should avoid forcing all dark regions into gray mids.";
    } else {
        subjectIntent.id = "automaticSceneBalance";
        subjectIntent.label = "Automatic Scene Balance";
        subjectIntent.reason =
            "No user importance brush is active; Auto is using a weak composition/detail prior while preserving the whole scene.";
    }
    ApplyDevelopSubjectRefinedMap(subjectIntent, importanceMap);
    subjectIntent.solveNotes = BuildDevelopSubjectSolveNotes(subjectIntent);
    return subjectIntent;
}

nlohmann::json DevelopSubjectSceneIntentToJson(const DevelopSubjectSceneIntent& subjectIntent) {
    return {
        { "version", kDevelopSubjectSceneIntentVersion },
        { "id", subjectIntent.id },
        { "label", subjectIntent.label },
        { "reason", subjectIntent.reason },
        { "solveNotesVersion", kDevelopSubjectImportanceSolveNotesVersion },
        { "solveNotes", subjectIntent.solveNotes },
        { "userGuidanceStatus", subjectIntent.userGuidanceStatus },
        { "userGuidanceActive", subjectIntent.userGuidanceActive },
        { "automaticOnly", subjectIntent.automaticOnly },
        { "userSubjectSceneBias", subjectIntent.userSubjectSceneBias },
        { "userMoodReadabilityBias", subjectIntent.userMoodReadabilityBias },
        { "userGuidanceStrength", subjectIntent.userGuidanceStrength },
        { "automaticConfidence", subjectIntent.automaticConfidence },
        { "centerPrior", subjectIntent.centerPrior },
        { "readabilityPressure", subjectIntent.readabilityPressure },
        { "protectionPressure", subjectIntent.protectionPressure },
        { "moodPreservationPressure", subjectIntent.moodPreservationPressure },
        { "subjectPriority", subjectIntent.subjectPriority },
        { "sceneIntegrity", subjectIntent.sceneIntegrity },
        { "improveReadability", subjectIntent.improveReadability },
        { "preserveMood", subjectIntent.preserveMood },
        { "subjectSceneAxis", subjectIntent.subjectSceneAxis },
        { "moodReadabilityAxis", subjectIntent.moodReadabilityAxis },
        { "importanceRegionCount", subjectIntent.importanceRegionCount },
        { "importanceStrokeCount", subjectIntent.importanceStrokeCount },
        { "importanceStrength", subjectIntent.importanceStrength },
        { "importanceImportant", subjectIntent.importanceImportant },
        { "importanceReveal", subjectIntent.importanceReveal },
        { "importanceProtect", subjectIntent.importanceProtect },
        { "importancePreserveMood", subjectIntent.importancePreserveMood },
        { "importanceIgnore", subjectIntent.importanceIgnore },
        { "importanceSubjectPriority", subjectIntent.importanceSubjectPriority },
        { "importanceReadability", subjectIntent.importanceReadability },
        { "importanceProtection", subjectIntent.importanceProtection },
        { "importanceMood", subjectIntent.importanceMood },
        { "importanceLowPriority", subjectIntent.importanceLowPriority },
        { "importanceMap", subjectIntent.importanceMap },
        { "importanceMapCoverage", subjectIntent.importanceMapCoverage },
        { "importanceMapPositiveCoverage", subjectIntent.importanceMapPositiveCoverage },
        { "importanceMapLowPriorityCoverage", subjectIntent.importanceMapLowPriorityCoverage },
        { "importanceMapRevealCoverage", subjectIntent.importanceMapRevealCoverage },
        { "importanceMapProtectCoverage", subjectIntent.importanceMapProtectCoverage },
        { "importanceMapMoodCoverage", subjectIntent.importanceMapMoodCoverage },
        { "importanceMapPeak", subjectIntent.importanceMapPeak },
        { "importanceMapConfidence", subjectIntent.importanceMapConfidence },
        { "importanceMapCenterBias", subjectIntent.importanceMapCenterBias },
        { "importanceMapEdgeBias", subjectIntent.importanceMapEdgeBias },
        { "refinedImportanceMap", subjectIntent.refinedImportanceMap },
        { "refinedMapCoverage", subjectIntent.refinedMapCoverage },
        { "refinedMapLowPriorityCoverage", subjectIntent.refinedMapLowPriorityCoverage },
        { "refinedMapReadabilityCoverage", subjectIntent.refinedMapReadabilityCoverage },
        { "refinedMapProtectionCoverage", subjectIntent.refinedMapProtectionCoverage },
        { "refinedMapMoodCoverage", subjectIntent.refinedMapMoodCoverage },
        { "refinedMapPeak", subjectIntent.refinedMapPeak },
        { "refinedMapConfidence", subjectIntent.refinedMapConfidence },
        { "refinedMapBoundaryHint", subjectIntent.refinedMapBoundaryHint },
        { "evidence", subjectIntent.evidence }
    };
}

DevelopDynamicRangeStrategy ResolveDevelopDynamicRangeStrategy(
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float tinySpecularAllowance) {
    DevelopDynamicRangeStrategy strategy;
    if (!stats.valid) {
        strategy.id = "pendingRenderAnalysis";
        strategy.label = "Pending Render Analysis";
        strategy.reason = "Develop needs a rendered analysis pass before it can name the highlight and shadow strategy.";
        strategy.highlightPolicy = "Use conservative highlight rolloff until render statistics are available.";
        strategy.shadowPolicy = "Avoid aggressive shadow lift until noise and tone statistics are available.";
        return strategy;
    }

    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;

    strategy.highlightImportance = SaturateFloat(
        stats.highlightPressure * 0.58f +
        stats.clippingRatio * 5.0f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        regionEvidence.meaningfulHighlightPressure * 0.18f +
        regionEvidence.broadHighlightPressure * 0.10f +
        hdrNeed * 0.18f +
        std::max(0.0f, guidance.highlightGuard) * 0.12f -
        tinySpecularAllowance * 0.12f);
    strategy.shadowReadability = SaturateFloat(
        shadowRescueNeed * 0.68f +
        regionEvidence.localShadowHotspotRisk * 0.12f +
        std::max(0.0f, guidance.shadowLift) * 0.16f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.10f -
        stats.noiseRisk * 0.20f -
        regionEvidence.shadowNoiseLiftRisk * 0.12f -
        (darkIntent ? 0.12f : 0.0f));
    strategy.noiseConstraint = SaturateFloat(
        stats.noiseRisk +
        regionEvidence.shadowNoiseLiftRisk * 0.18f +
        std::max(0.0f, guidance.shadowLift) * 0.18f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.08f);
    strategy.rangeCompression = SaturateFloat(
        hdrNeed * 0.50f +
        regionEvidence.localRangeConflict * 0.16f +
        regionEvidence.localEvConflict * 0.12f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.24f +
        std::max(0.0f, guidance.highlightGuard) * 0.10f +
        std::max(0.0f, guidance.shadowLift) * 0.08f);
    strategy.brightnessHierarchyRisk = SaturateFloat(
        strategy.rangeCompression * 0.34f +
        regionEvidence.brightnessHierarchyRisk * 0.22f +
        regionEvidence.highlightGrayRisk * 0.18f +
        regionEvidence.flatGrayRisk * 0.10f +
        flatSceneNeed * 0.20f +
        stats.highlightPressure * 0.18f -
        std::max(0.0f, guidance.contrastBias) * 0.10f -
        std::max(0.0f, guidance.highlightCharacter) * 0.08f);
    strategy.smallSpecularClippingAllowed =
        tinySpecularAllowance > 0.42f &&
        stats.clippingRatio < 0.012f &&
        (!regionEvidence.valid || regionEvidence.smallSpecularLikely) &&
        regionEvidence.meaningfulHighlightPressure < 0.32f &&
        !rangeIntent;
    strategy.specularHighlightToleranceNeed = SaturateFloat(
        tinySpecularAllowance * 0.52f +
        (regionEvidence.smallSpecularLikely ? 0.22f : 0.0f) +
        ((naturalIntent || brightIntent || punchyIntent) ? 0.08f : 0.0f) -
        stats.highlightPressure * 0.20f -
        regionEvidence.broadHighlightPressure * 0.22f -
        regionEvidence.meaningfulHighlightPressure * 0.18f -
        SaturateFloat((stats.clippingRatio - 0.006f) / 0.020f) * 0.35f -
        (rangeIntent ? 0.16f : 0.0f) -
        (flatIntent ? 0.08f : 0.0f));
    strategy.broadHighlightGuardNeed = SaturateFloat(
        strategy.highlightImportance * 0.34f +
        regionEvidence.broadHighlightPressure * 0.24f +
        regionEvidence.meaningfulHighlightPressure * 0.18f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        stats.clippingRatio * 2.60f +
        hdrNeed * 0.12f +
        (rangeIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.04f : 0.0f) -
        tinySpecularAllowance * 0.18f -
        strategy.specularHighlightToleranceNeed * 0.10f -
        (punchyIntent ? 0.03f : 0.0f));
    strategy.naturalContrastGuardNeed = SaturateFloat(
        strategy.brightnessHierarchyRisk * 0.42f +
        regionEvidence.brightnessHierarchyRisk * 0.20f +
        regionEvidence.highlightGrayRisk * 0.18f +
        regionEvidence.flatGrayRisk * 0.20f +
        flatSceneNeed * 0.16f +
        stats.textureConfidence * 0.06f +
        (naturalIntent ? 0.06f : 0.0f) +
        (punchyIntent ? 0.08f : 0.0f) -
        strategy.broadHighlightGuardNeed * 0.06f -
        (rangeIntent ? 0.10f : 0.0f) -
        (flatIntent ? 0.12f : 0.0f));
    strategy.brightHighlightRolloffNeed = SaturateFloat(
        strategy.highlightImportance * 0.54f +
        strategy.brightnessHierarchyRisk * 0.26f +
        regionEvidence.highlightGrayRisk * 0.08f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        (brightIntent ? 0.08f : 0.0f) +
        (punchyIntent ? 0.06f : 0.0f) -
        stats.clippingRatio * 2.0f -
        (rangeIntent ? 0.04f : 0.0f));
    strategy.highlightBrightnessAnchorNeed = SaturateFloat(
        strategy.highlightImportance * 0.24f +
        strategy.brightnessHierarchyRisk * 0.22f +
        regionEvidence.highlightGrayRisk * 0.22f +
        strategy.broadHighlightGuardNeed * 0.16f +
        strategy.naturalContrastGuardNeed * 0.14f +
        strategy.brightHighlightRolloffNeed * 0.14f +
        regionEvidence.broadHighlightPressure * 0.08f +
        (naturalIntent ? 0.05f : 0.0f) +
        (brightIntent ? 0.08f : 0.0f) +
        (punchyIntent ? 0.05f : 0.0f) -
        strategy.specularHighlightToleranceNeed * 0.06f -
        stats.clippingRatio * 1.20f -
        regionEvidence.localHaloRisk * 0.04f -
        (rangeIntent ? 0.06f : 0.0f) -
        (flatIntent ? 0.04f : 0.0f));
    strategy.shadowNoiseFloorNeed = SaturateFloat(
        strategy.noiseConstraint * 0.42f +
        regionEvidence.shadowNoiseLiftRisk * 0.30f +
        regionEvidence.localShadowHotspotRisk * 0.12f +
        darkness * 0.14f +
        (darkIntent ? 0.08f : 0.0f) -
        strategy.shadowReadability * 0.18f -
        (rangeIntent ? 0.08f : 0.0f) -
        (flatIntent ? 0.04f : 0.0f));
    strategy.shadowReadabilityLiftNeed = SaturateFloat(
        strategy.shadowReadability * 0.42f +
        shadowRescueNeed * 0.22f +
        regionEvidence.localShadowHotspotRisk * 0.16f +
        SaturateFloat((0.74f - stats.noiseRisk) / 0.74f) * 0.16f +
        stats.textureConfidence * 0.08f +
        (rangeIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.06f : 0.0f) +
        (brightIntent ? 0.05f : 0.0f) -
        strategy.noiseConstraint * 0.20f -
        regionEvidence.shadowNoiseLiftRisk * 0.18f -
        strategy.shadowNoiseFloorNeed * 0.12f -
        (darkIntent ? 0.06f : 0.0f) -
        (punchyIntent ? 0.04f : 0.0f));
    strategy.localHaloGuardNeed = SaturateFloat(
        regionEvidence.localHaloRisk * 0.52f +
        regionEvidence.localRangeConflict * 0.18f +
        regionEvidence.localEvConflict * 0.14f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        regionEvidence.localShadowHotspotRisk * 0.08f +
        strategy.rangeCompression * 0.10f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.08f -
        (punchyIntent ? 0.04f : 0.0f));
    strategy.localHighlightHotspotRisk = regionEvidence.localHighlightHotspotRisk;
    strategy.localShadowHotspotRisk = regionEvidence.localShadowHotspotRisk;
    strategy.localRangeConflict = regionEvidence.localRangeConflict;
    strategy.localEvConflict = regionEvidence.localEvConflict;
    strategy.localHaloRisk = regionEvidence.localHaloRisk;
    strategy.flatGrayRisk = regionEvidence.flatGrayRisk;
    strategy.highlightGrayRisk = regionEvidence.highlightGrayRisk;
    strategy.meaningfulHighlightPressure = regionEvidence.meaningfulHighlightPressure;
    strategy.localExposureHighlightCrowding = regionEvidence.localExposureHighlightCrowding;
    strategy.localExposureShadowCrowding = regionEvidence.localExposureShadowCrowding;
    strategy.localExposureHaloStress = regionEvidence.localExposureHaloStress;
    strategy.localExposureFlatnessRisk = regionEvidence.localExposureFlatnessRisk;
    strategy.localExposureDamageRisk = regionEvidence.localExposureDamageRisk;
    strategy.regionEvidence = DevelopDynamicRangeRegionEvidenceToJson(regionEvidence);
    ResolveDevelopDynamicRangeStrategyMap(
        strategy,
        intent,
        guidance,
        stats,
        regionEvidence,
        shadowRescueNeed,
        hdrNeed,
        tinySpecularAllowance);
    ResolveDevelopLocalExposureStrategy(
        strategy,
        intent,
        stats,
        regionEvidence,
        shadowRescueNeed,
        hdrNeed);

    if (rangeIntent || (hdrNeed > 0.62f && strategy.highlightImportance > 0.42f)) {
        strategy.id = "maximumVisibleRange";
        strategy.label = "Maximum Visible Range";
        strategy.reason = "Auto is prioritizing range fitting with extra highlight and shadow guardrails for the selected intent.";
        strategy.highlightPolicy = "Compress broad highlights without claiming clipped-data recovery.";
        strategy.shadowPolicy = "Lift only shadows that clear the current noise and texture checks.";
    } else if (regionEvidence.valid &&
        strategy.localHaloGuardNeed > 0.46f &&
        regionEvidence.localHaloRisk > 0.34f) {
        strategy.id = "haloSafeLocalRange";
        strategy.label = "Halo-Safe Local Range";
        strategy.reason = "Rendered regional evidence shows halo or edge-risk pressure, so Auto should test safer Scene Prep range shaping before pushing local exposure harder.";
        strategy.highlightPolicy = "Protect local highlight transitions with stronger halo and smooth-gradient guardrails, without claiming clipped-data recovery.";
        strategy.shadowPolicy = "Avoid opening shadows across strong edges when that would create local glow or artificial relighting.";
    } else if (regionEvidence.valid &&
        (regionEvidence.localRangeConflict > 0.58f || regionEvidence.localEvConflict > 0.54f) &&
        (regionEvidence.localHaloRisk > 0.32f || regionEvidence.localEvConflict > 0.60f)) {
        strategy.id = "localizedRangeGuard";
        strategy.label = "Localized Range Guard";
        strategy.reason = "Rendered regional evidence shows local EV conflict near risky local range transitions, so Auto should prefer controlled local exposure over broad flattening.";
        strategy.highlightPolicy = "Restrain local highlight shaping where regional damage or halo risk is concentrated.";
        strategy.shadowPolicy = "Open shadows selectively and avoid pushing local range hard enough to create halos or noisy gray patches.";
    } else if (strategy.broadHighlightGuardNeed > 0.52f &&
        !strategy.smallSpecularClippingAllowed) {
        strategy.id = "broadHighlightGuard";
        strategy.label = "Broad Highlight Guard";
        strategy.reason = "Auto sees broad meaningful highlight pressure, so it should test local highlight compression instead of treating bright areas like disposable glints.";
        strategy.highlightPolicy = "Use Scene Prep to protect broad bright regions and smooth transitions without claiming clipped-data recovery.";
        strategy.shadowPolicy = "Avoid sacrificing useful mids and shadows unless broad highlight evidence really needs the headroom.";
    } else if (strategy.shadowNoiseFloorNeed > 0.54f &&
        (strategy.noiseConstraint > 0.58f ||
            regionEvidence.shadowNoiseLiftRisk > 0.54f ||
            darkIntent)) {
        strategy.id = "shadowNoiseFloor";
        strategy.label = "Shadow Noise Floor";
        strategy.reason = "Auto is testing whether noisy or low-value dark regions should stay darker instead of being lifted into gray noise.";
        strategy.highlightPolicy = "Keep highlight guard active while avoiding a broad exposure move that would force the shadow floor upward.";
        strategy.shadowPolicy = "Hold noisy shadows down with Scene Prep limits unless rendered evidence proves the shadows are clean and important.";
    } else if (strategy.shadowReadabilityLiftNeed > 0.50f &&
        strategy.noiseConstraint < 0.66f &&
        strategy.shadowNoiseFloorNeed < 0.48f) {
        strategy.id = "shadowReadabilityLift";
        strategy.label = "Shadow Readability Lift";
        strategy.reason = "Auto sees shadows that look readable enough to open locally, so it should test Scene Prep shadow lift without moving the RAW baseline.";
        strategy.highlightPolicy = "Keep highlight guard active while local shadow and midtone support is tested.";
        strategy.shadowPolicy = "Open clean meaningful shadows locally, with noise and halo guardrails still active.";
    } else if (darkIntent || (darkness > 0.42f && strategy.noiseConstraint > 0.54f)) {
        strategy.id = "moodPreservingRange";
        strategy.label = "Mood-Preserving Range";
        strategy.reason = "Auto is preserving low-key hierarchy because broad shadow lift would risk gray, noisy darkness.";
        strategy.highlightPolicy = "Keep bright accents controlled while preserving the dark scene mood.";
        strategy.shadowPolicy = "Hold noisy or mood-critical shadows darker unless rendered evidence asks for selective cleanup.";
    } else if (strategy.highlightImportance > 0.52f && strategy.shadowReadability > 0.34f) {
        strategy.id = "highlightMidsBalance";
        strategy.label = "Highlight / Midtone Balance";
        strategy.reason = "Auto sees both broad highlight pressure and useful mids/shadows, so it is testing lower placement with local support.";
        strategy.highlightPolicy = "Protect broad highlights first, then support mids locally.";
        strategy.shadowPolicy = "Use local exposure rather than a broad global lift where possible.";
    } else if (strategy.highlightBrightnessAnchorNeed > 0.40f &&
        !strategy.smallSpecularClippingAllowed) {
        strategy.id = "luminousHighlightAnchor";
        strategy.label = "Luminous Highlight Anchor";
        strategy.reason = "Auto sees protected highlight areas that may be flattening toward gray, so it should test finish-tone separation that keeps bright regions feeling bright.";
        strategy.highlightPolicy = "Anchor protected highlights with downstream shoulder and contrast shape; this preserves brightness feeling and does not recover clipped detail.";
        strategy.shadowPolicy = "Keep surrounding mids and darks separated enough that bright regions still read as light.";
    } else if (strategy.brightHighlightRolloffNeed > 0.45f) {
        strategy.id = "brightHighlightRolloff";
        strategy.label = "Bright Highlight Rolloff";
        strategy.reason = "Auto is guarding highlights but keeping a candidate that preserves the feeling of bright light instead of flattening highlights toward gray.";
        strategy.highlightPolicy = "Use finish-tone rolloff to keep highlights bright and smooth; tiny specular clipping may remain acceptable.";
        strategy.shadowPolicy = "Avoid trading believable highlight brightness for unnecessary global shadow lift.";
    } else if (strategy.naturalContrastGuardNeed > 0.42f &&
        !rangeIntent &&
        !flatIntent) {
        strategy.id = "naturalContrastGuard";
        strategy.label = "Natural Contrast Guard";
        strategy.reason = "Auto sees range compression or flat-gray risk, so it should test restoring natural separation in finish tone instead of adding more local exposure.";
        strategy.highlightPolicy = "Keep bright areas separated from mids with downstream shoulder/contrast shaping, without claiming highlight recovery.";
        strategy.shadowPolicy = "Let unimportant darks keep depth while protecting readable shadows from being crushed.";
    } else if (strategy.smallSpecularClippingAllowed &&
        strategy.specularHighlightToleranceNeed > 0.34f) {
        strategy.id = "specularHighlightTolerance";
        strategy.label = "Specular Highlight Tolerance";
        strategy.reason = "Auto sees mostly tiny point-source clipping, so it can test keeping glints bright instead of pulling the whole image down for them.";
        strategy.highlightPolicy = "Allow tiny specular cores to clip when broad highlight evidence is low, while keeping smooth finish-tone rolloff around them.";
        strategy.shadowPolicy = "Do not trade midtone or shadow placement away just to save tiny, low-importance glints.";
    } else if (strategy.shadowReadability > 0.50f && strategy.noiseConstraint < 0.70f) {
        strategy.id = "shadowReadability";
        strategy.label = "Shadow Readability";
        strategy.reason = "Auto is opening readable shadows because the selected intent and noise estimate allow it.";
        strategy.highlightPolicy = "Keep highlight guard active while allowing mids and shadows to rise.";
        strategy.shadowPolicy = "Open meaningful shadows with noise-aware limits.";
    } else if (punchyIntent) {
        strategy.id = "contrastHierarchy";
        strategy.label = "Contrast Hierarchy";
        strategy.reason = "Auto is preserving punch and endpoint separation while still watching highlight and shadow damage.";
        strategy.highlightPolicy = "Allow brighter highlights and deeper darks where rendered metrics stay believable.";
        strategy.shadowPolicy = "Let unimportant darkness stay dark when it supports the selected contrast intent.";
    }

    return strategy;
}

nlohmann::json DevelopDynamicRangeStrategyToJson(const DevelopDynamicRangeStrategy& strategy) {
    return {
        { "version", kDevelopDynamicRangeStrategyVersion },
        { "id", strategy.id },
        { "label", strategy.label },
        { "reason", strategy.reason },
        { "highlightPolicy", strategy.highlightPolicy },
        { "shadowPolicy", strategy.shadowPolicy },
        { "highlightImportance", strategy.highlightImportance },
        { "shadowReadability", strategy.shadowReadability },
        { "noiseConstraint", strategy.noiseConstraint },
        { "rangeCompression", strategy.rangeCompression },
        { "brightnessHierarchyRisk", strategy.brightnessHierarchyRisk },
        { "meaningfulHighlightPressure", strategy.meaningfulHighlightPressure },
        { "naturalContrastGuardNeed", strategy.naturalContrastGuardNeed },
        { "brightHighlightRolloffNeed", strategy.brightHighlightRolloffNeed },
        { "highlightBrightnessAnchorNeed", strategy.highlightBrightnessAnchorNeed },
        { "broadHighlightGuardNeed", strategy.broadHighlightGuardNeed },
        { "specularHighlightToleranceNeed", strategy.specularHighlightToleranceNeed },
        { "shadowReadabilityLiftNeed", strategy.shadowReadabilityLiftNeed },
        { "shadowNoiseFloorNeed", strategy.shadowNoiseFloorNeed },
        { "localHighlightHotspotRisk", strategy.localHighlightHotspotRisk },
        { "localShadowHotspotRisk", strategy.localShadowHotspotRisk },
        { "localRangeConflict", strategy.localRangeConflict },
        { "localEvConflict", strategy.localEvConflict },
        { "localHaloRisk", strategy.localHaloRisk },
        { "localHaloGuardNeed", strategy.localHaloGuardNeed },
        { "flatGrayRisk", strategy.flatGrayRisk },
        { "highlightGrayRisk", strategy.highlightGrayRisk },
        { "strategyMap", {
            { "version", kDevelopDynamicRangeStrategyMapVersion },
            { "highlightShadowAxis", strategy.strategyMapHighlightShadowAxis },
            { "contrastRangeAxis", strategy.strategyMapContrastRangeAxis },
            { "highlightPriority", strategy.strategyMapHighlightPriority },
            { "shadowVisibility", strategy.strategyMapShadowVisibility },
            { "naturalContrast", strategy.strategyMapNaturalContrast },
            { "visibleRange", strategy.strategyMapVisibleRange },
            { "reason", strategy.strategyMapReason }
        } },
        { "strategyMapVersion", kDevelopDynamicRangeStrategyMapVersion },
        { "strategyMapHighlightShadowAxis", strategy.strategyMapHighlightShadowAxis },
        { "strategyMapContrastRangeAxis", strategy.strategyMapContrastRangeAxis },
        { "strategyMapHighlightPriority", strategy.strategyMapHighlightPriority },
        { "strategyMapShadowVisibility", strategy.strategyMapShadowVisibility },
        { "strategyMapNaturalContrast", strategy.strategyMapNaturalContrast },
        { "strategyMapVisibleRange", strategy.strategyMapVisibleRange },
        { "strategyMapReason", strategy.strategyMapReason },
        { "localExposureStrategy", {
            { "version", kDevelopLocalExposureStrategyVersion },
            { "id", strategy.localExposureStrategyId },
            { "label", strategy.localExposureStrategyLabel },
            { "reason", strategy.localExposureStrategyReason },
            { "rangeRedistribution", strategy.localExposureRangeRedistribution },
            { "highlightCompression", strategy.localExposureHighlightCompression },
            { "shadowOpening", strategy.localExposureShadowOpening },
            { "noiseGuard", strategy.localExposureNoiseGuard },
            { "haloGuard", strategy.localExposureHaloGuard },
            { "textureGuard", strategy.localExposureTextureGuard },
            { "shadowEvBudget", strategy.localExposureShadowEvBudget },
            { "highlightEvBudget", strategy.localExposureHighlightEvBudget },
            { "strengthTarget", strategy.localExposureStrengthTarget },
            { "highlightCrowding", strategy.localExposureHighlightCrowding },
            { "shadowCrowding", strategy.localExposureShadowCrowding },
            { "haloStress", strategy.localExposureHaloStress },
            { "flatnessRisk", strategy.localExposureFlatnessRisk },
            { "damageRisk", strategy.localExposureDamageRisk }
        } },
        { "localExposureStrategyVersion", kDevelopLocalExposureStrategyVersion },
        { "localExposureStrategyId", strategy.localExposureStrategyId },
        { "localExposureStrategyLabel", strategy.localExposureStrategyLabel },
        { "localExposureStrategyReason", strategy.localExposureStrategyReason },
        { "localExposureRangeRedistribution", strategy.localExposureRangeRedistribution },
        { "localExposureHighlightCompression", strategy.localExposureHighlightCompression },
        { "localExposureShadowOpening", strategy.localExposureShadowOpening },
        { "localExposureNoiseGuard", strategy.localExposureNoiseGuard },
        { "localExposureHaloGuard", strategy.localExposureHaloGuard },
        { "localExposureTextureGuard", strategy.localExposureTextureGuard },
        { "localExposureShadowEvBudget", strategy.localExposureShadowEvBudget },
        { "localExposureHighlightEvBudget", strategy.localExposureHighlightEvBudget },
        { "localExposureStrengthTarget", strategy.localExposureStrengthTarget },
        { "localExposureHighlightCrowding", strategy.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", strategy.localExposureShadowCrowding },
        { "localExposureHaloStress", strategy.localExposureHaloStress },
        { "localExposureFlatnessRisk", strategy.localExposureFlatnessRisk },
        { "localExposureDamageRisk", strategy.localExposureDamageRisk },
        { "smallSpecularClippingAllowed", strategy.smallSpecularClippingAllowed },
        { "regionEvidence", strategy.regionEvidence }
    };
}

struct DevelopConvergenceAdmissionPolicy {
    float baseMinimumImprovement = 0.025f;
    float minimumImprovement = 0.025f;
    bool tightened = false;
    std::string reason = "base";
    std::string evidenceState;
    std::string evidenceDecision;
    int evidencePass = 0;
};

DevelopConvergenceAdmissionPolicy ResolveDevelopConvergenceAdmissionPolicy(
    const nlohmann::json& previousToneJson,
    int previousPass,
    bool hasRefineIntent) {
    DevelopConvergenceAdmissionPolicy policy;
    policy.evidencePass = previousPass;
    const nlohmann::json evidence =
        previousToneJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    if (evidence.is_object()) {
        policy.evidenceState = evidence.value("state", std::string());
        policy.evidenceDecision = evidence.value("decision", std::string());
        policy.evidencePass = evidence.value("pass", previousPass);
    }
    const nlohmann::json continuationPolicy =
        previousToneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const std::string continuationDecision =
        continuationPolicy.is_object()
            ? continuationPolicy.value("decision", std::string())
            : std::string();
    const int continuationPass =
        continuationPolicy.is_object()
            ? continuationPolicy.value("pass", previousPass)
            : previousPass;

    if (hasRefineIntent) {
        policy.reason = "refineIntentUsesDedicatedOscillationGuards";
        return policy;
    }

    const int activePass =
        std::max({ previousPass, policy.evidencePass, continuationPass });
    const bool continuedFeedbackApplied =
        policy.evidenceState == "continuing" ||
        policy.evidenceDecision == "continue" ||
        continuationDecision == "continue";
    const bool continuedSolveAwaitingMetrics =
        activePass > 0 &&
        policy.evidenceState == "awaitingRenderedMetrics" &&
        policy.evidenceDecision == "waitForRenderedMetrics" &&
        continuationDecision == "waitForRenderedMetrics";
    if (continuedFeedbackApplied || continuedSolveAwaitingMetrics) {
        policy.tightened = true;
        if (activePass >= 2) {
            policy.minimumImprovement += 0.020f;
            policy.reason = "lateContinuationRequiresStrongerImprovement";
        } else {
            policy.minimumImprovement += 0.010f;
            policy.reason = continuedSolveAwaitingMetrics
                ? "continuedSolveMetricsRequireClearerImprovement"
                : "continuedRenderedFeedbackRequiresClearerImprovement";
        }
    }
    return policy;
}

nlohmann::json BuildDevelopRenderedContinuationPolicyRecord(
    const std::string& decision,
    const std::string& reason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    int pass,
    int nextPass,
    const std::string& stageFocus,
    const std::string& stageReason,
    float improvement,
    const std::string& stabilityStatus,
    const std::string& trendStatus,
    const std::string& monotonicGuardStatus) {
    const int clampedPass =
        std::clamp(pass, 0, kDevelopRenderedFeedbackMaxPasses);
    const int clampedNextPass =
        std::clamp(nextPass, clampedPass, kDevelopRenderedFeedbackMaxPasses);
    return {
        { "version", kDevelopRenderedContinuationVersion },
        { "decision", decision },
        { "reason", reason },
        { "nextStep", nextStep },
        { "requiresAutoSolve", requiresAutoSolve },
        { "requiresRenderedMetrics", requiresRenderedMetrics },
        { "shouldContinue", decision == "continue" || decision == "waitForRenderedMetrics" },
        { "bounded", true },
        { "pass", clampedPass },
        { "nextPass", clampedNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "remainingPasses", std::max(0, kDevelopRenderedFeedbackMaxPasses - clampedNextPass) },
        { "stageFocus", stageFocus },
        { "stageReason", stageReason },
        { "evidence", {
            { "improvement", improvement },
            { "stabilityStatus", stabilityStatus },
            { "trendStatus", trendStatus },
            { "monotonicGuardStatus", monotonicGuardStatus }
        } }
    };
}

nlohmann::json BuildDevelopAutoConvergenceEvidenceRecord(
    const DevelopAutoCandidateSolveResult& result,
    bool renderedMetricsMatchCurrentSolve,
    bool renderedMetricsReadyForCurrentSolve,
    const std::string& currentRenderMetricsStatus,
    const std::string& loopState,
    const std::string& loopAction,
    const std::string& loopStopReason,
    const std::string& loopNextStep,
    bool loopRequiresAutoSolve,
    bool loopRequiresRenderedMetrics,
    int loopPass,
    int loopNextPass,
    int renderedHistoryCount,
    const nlohmann::json& continuationPolicy,
    const nlohmann::json& toneJson) {
    std::string state;
    std::string decision;
    std::string reason;
    if (result.renderedFeedbackApplied) {
        state = "continuing";
        decision = "continue";
        reason = result.renderedFeedbackAction.empty()
            ? std::string("renderedFeedbackApplied")
            : result.renderedFeedbackAction;
    } else if (renderedMetricsReadyForCurrentSolve &&
        !result.renderedFeedbackStopReason.empty()) {
        state = result.renderedFeedbackStopIsConverged ? "converged" : "stopped";
        decision = "stop";
        reason = result.renderedFeedbackStopReason;
    } else if (!renderedMetricsMatchCurrentSolve) {
        state = "awaitingRenderedMetrics";
        decision = "waitForRenderedMetrics";
        reason = "awaitingRenderedMetrics";
    } else if (result.converged) {
        state = "parameterStable";
        decision = "stop";
        reason = "candidateSolveFingerprintStable";
    } else {
        state = "evaluating";
        decision = continuationPolicy.value("decision", std::string("stop"));
        reason = loopStopReason.empty() ? std::string("renderedMetricsMeasured") : loopStopReason;
    }

    const bool shouldContinue =
        decision == "continue" ||
        decision == "waitForRenderedMetrics" ||
        continuationPolicy.value("shouldContinue", false);
    const nlohmann::json continuationEvidence =
        continuationPolicy.value("evidence", nlohmann::json::object());

    return {
        { "version", kDevelopConvergenceEvidenceVersion },
        { "state", state },
        { "decision", decision },
        { "reason", reason },
        { "shouldContinue", shouldContinue },
        { "bounded", true },
        { "pass", loopPass },
        { "nextPass", loopNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "remainingPasses", continuationPolicy.value(
            "remainingPasses",
            std::max(0, kDevelopRenderedFeedbackMaxPasses - loopNextPass)) },
        { "nextStep", loopNextStep },
        { "requiresAutoSolve", loopRequiresAutoSolve },
        { "requiresRenderedMetrics", loopRequiresRenderedMetrics },
        { "parameter", {
            { "converged", result.converged },
            { "convergencePass", result.convergencePass },
            { "solveFingerprint", result.fingerprint },
            { "selectedId", result.selectedId },
            { "selectedScore", result.selectedScore }
        } },
        { "admission", {
            { "version", kDevelopConvergenceAdmissionVersion },
            { "baseMinimumImprovement", result.renderedFeedbackAdmissionBaseMinimumImprovement },
            { "minimumImprovement", result.renderedFeedbackAdmissionMinimumImprovement },
            { "tightened", result.renderedFeedbackAdmissionTightened },
            { "reason", result.renderedFeedbackAdmissionReason },
            { "evidenceState", result.renderedFeedbackAdmissionEvidenceState },
            { "evidenceDecision", result.renderedFeedbackAdmissionEvidenceDecision },
            { "evidencePass", result.renderedFeedbackAdmissionEvidencePass }
        } },
        { "rendered", {
            { "metricsStatus", currentRenderMetricsStatus },
            { "metricsMatchCurrentSolve", renderedMetricsMatchCurrentSolve },
            { "metricsReadyForCurrentSolve", renderedMetricsReadyForCurrentSolve },
            { "feedbackApplied", result.renderedFeedbackApplied },
            { "feedbackAction", result.renderedFeedbackAction },
            { "stopReason", result.renderedFeedbackStopReason },
            { "stopConverged", result.renderedFeedbackStopIsConverged },
            { "improvement", result.renderedFeedbackImprovement },
            { "stabilityStatus", toneJson.value("autoCandidateRenderedStabilityStatus", std::string()) },
            { "stabilityDistance", result.renderedFeedbackStabilityDistance },
            { "stabilityScoreDelta", result.renderedFeedbackStabilityScoreDelta },
            { "trendStatus", toneJson.value("autoCandidateRenderedTrendStatus", std::string()) },
            { "trendHistoryCount", result.renderedFeedbackTrendHistoryCount },
            { "trendSameBestCount", result.renderedFeedbackTrendSameBestCount },
            { "trendScoreSpread", result.renderedFeedbackTrendScoreSpread },
            { "trendNearestDistance", result.renderedFeedbackTrendNearestDistance },
            { "monotonicGuardStatus", toneJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()) },
            { "monotonicMetric", result.renderedFeedbackMonotonicMetric },
            { "revisionStage", toneJson.value("autoCandidateRenderedRevisionStage", std::string()) },
            { "revisionReason", toneJson.value("autoCandidateRenderedRevisionReason", std::string()) },
            { "historyCount", renderedHistoryCount }
        } },
        { "loop", {
            { "state", loopState },
            { "action", loopAction },
            { "stopReason", loopStopReason },
            { "nextStep", loopNextStep },
            { "requiresAutoSolve", loopRequiresAutoSolve },
            { "requiresRenderedMetrics", loopRequiresRenderedMetrics }
        } },
        { "continuation", {
            { "version", continuationPolicy.value("version", std::string()) },
            { "decision", continuationPolicy.value("decision", std::string()) },
            { "reason", continuationPolicy.value("reason", std::string()) },
            { "nextStep", continuationPolicy.value("nextStep", std::string()) },
            { "shouldContinue", continuationPolicy.value("shouldContinue", false) },
            { "stageFocus", continuationPolicy.value("stageFocus", std::string()) },
            { "stageReason", continuationPolicy.value("stageReason", std::string()) },
            { "evidence", continuationEvidence }
        } }
    };
}

struct DevelopAutoStageFingerprints {
    std::uint64_t metadata = 0;
    std::uint64_t rawBase = 0;
    std::uint64_t rawGlobal = 0;
    std::uint64_t scenePrep = 0;
    std::uint64_t finishTone = 0;
    std::uint64_t finalValidation = 0;
};

struct DevelopAutoStageHashBuilder {
    std::uint64_t hash = 1469598103934665603ull;

    void AddValue(std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    }

    void AddBool(bool value) {
        AddValue(value ? 1ull : 0ull);
    }

    void AddInt(int value) {
        AddValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    }

    void AddFloat(float value, float scale = 1000.0f) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        AddInt(quantized);
    }

    void AddString(const std::string& value) {
        for (unsigned char ch : value) {
            AddValue(static_cast<std::uint64_t>(ch));
        }
    }
};

std::string DevelopAutoStageStateForRevisionStage(const std::string& stage) {
    if (stage == "rawCleanup") {
        return "RENDER_RAW_BASE";
    }
    if (stage == "rawGlobal") {
        return "SOLVE_GLOBAL";
    }
    if (stage == "scenePrep") {
        return "SOLVE_SCENE_PREP";
    }
    if (stage == "finishTone") {
        return "SOLVE_FINISH_TONE";
    }
    if (stage == "converged") {
        return "CONVERGED";
    }
    if (stage == "none") {
        return "VALIDATE_FINAL";
    }
    return "SOLVE_GLOBAL";
}

int DevelopAutoStageOrder(const std::string& stageState) {
    if (stageState == "NEED_SOURCE") return 0;
    if (stageState == "METADATA_BOOTSTRAP") return 1;
    if (stageState == "RENDER_RAW_BASE") return 2;
    if (stageState == "ANALYZE_RAW_BASE") return 3;
    if (stageState == "SOLVE_GLOBAL") return 4;
    if (stageState == "RENDER_GLOBAL_BASE") return 5;
    if (stageState == "SOLVE_SCENE_PREP") return 6;
    if (stageState == "RENDER_PREFINISH") return 7;
    if (stageState == "ANALYZE_PREFINISH") return 8;
    if (stageState == "SOLVE_FINISH_TONE") return 9;
    if (stageState == "RENDER_FINAL") return 10;
    if (stageState == "VALIDATE_FINAL") return 11;
    if (stageState == "CONVERGED") return 12;
    return 4;
}

std::string DevelopAutoStageForChangedFingerprint(
    const nlohmann::json& previousFingerprints,
    const DevelopAutoStageFingerprints& current) {
    if (!previousFingerprints.is_object()) {
        return "METADATA_BOOTSTRAP";
    }
    if (previousFingerprints.value("metadata", static_cast<std::uint64_t>(0)) != current.metadata) {
        return "METADATA_BOOTSTRAP";
    }
    if (previousFingerprints.value("rawBase", static_cast<std::uint64_t>(0)) != current.rawBase) {
        return "RENDER_RAW_BASE";
    }
    if (previousFingerprints.value("rawGlobal", static_cast<std::uint64_t>(0)) != current.rawGlobal) {
        return "SOLVE_GLOBAL";
    }
    if (previousFingerprints.value("scenePrep", static_cast<std::uint64_t>(0)) != current.scenePrep) {
        return "SOLVE_SCENE_PREP";
    }
    if (previousFingerprints.value("finishTone", static_cast<std::uint64_t>(0)) != current.finishTone) {
        return "SOLVE_FINISH_TONE";
    }
    if (previousFingerprints.value("finalValidation", static_cast<std::uint64_t>(0)) != current.finalValidation) {
        return "VALIDATE_FINAL";
    }
    return "CONVERGED";
}

std::string DevelopAutoStagePassKind(int pass) {
    if (pass <= 0) {
        return "metadataBootstrap";
    }
    if (pass == 1) {
        return "statsSolve";
    }
    if (pass == 2) {
        return "renderedFeedbackRefine";
    }
    return "emergencyStabilization";
}

std::string DevelopAutoStageStatusFor(
    const std::string& stageState,
    const std::string& earliestStage,
    bool finalConverged,
    bool awaitingRenderedMetrics) {
    if (finalConverged) {
        return stageState == "CONVERGED" ? "complete" : "validated";
    }
    if (stageState == "CONVERGED") {
        return awaitingRenderedMetrics ? "awaitingRenderedMetrics" : "notReached";
    }
    if (stageState == "VALIDATE_FINAL" && awaitingRenderedMetrics) {
        return "awaitingRenderedMetrics";
    }
    const int stageOrder = DevelopAutoStageOrder(stageState);
    const int earliestOrder = DevelopAutoStageOrder(earliestStage);
    return stageOrder < earliestOrder ? "reused" : "complete";
}

DevelopAutoStageFingerprints BuildDevelopAutoStageFingerprints(
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings,
    const Raw::RawDetailFusionSettings& prepSettings,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopAutoCandidateSolveResult& result,
    const DevelopToneAutoStats& stats) {
    DevelopAutoStageFingerprints fingerprints;

    DevelopAutoStageHashBuilder metadataHash;
    metadataHash.AddString(metadata.sourcePath);
    metadataHash.AddString(metadata.cameraMake);
    metadataHash.AddString(metadata.cameraModel);
    metadataHash.AddInt(metadata.rawWidth);
    metadataHash.AddInt(metadata.rawHeight);
    metadataHash.AddInt(metadata.visibleWidth);
    metadataHash.AddInt(metadata.visibleHeight);
    metadataHash.AddInt(metadata.orientation);
    metadataHash.AddInt(metadata.bitDepth);
    metadataHash.AddInt(static_cast<int>(metadata.cfaPattern));
    metadataHash.AddInt(static_cast<int>(metadata.pixelLayout));
    metadataHash.AddBool(metadata.mosaiced);
    metadataHash.AddBool(metadata.isDng);
    metadataHash.AddBool(metadata.hasDngBaselineExposure);
    metadataHash.AddFloat(metadata.dngBaselineExposure, 1000.0f);
    metadataHash.AddFloat(metadata.blackLevel, 1000.0f);
    metadataHash.AddFloat(metadata.whiteLevel, 1000.0f);
    for (float value : metadata.perChannelBlack) metadataHash.AddFloat(value, 1000.0f);
    for (float value : metadata.cameraWhiteBalance) metadataHash.AddFloat(value, 1000.0f);
    for (float value : metadata.daylightWhiteBalance) metadataHash.AddFloat(value, 1000.0f);
    fingerprints.metadata = metadataHash.hash;

    DevelopAutoStageHashBuilder rawBaseHash;
    rawBaseHash.AddValue(fingerprints.metadata);
    rawBaseHash.AddFloat(settings.exposureStops, 1000.0f);
    rawBaseHash.AddInt(static_cast<int>(settings.whiteBalanceMode));
    for (float value : settings.manualWhiteBalance) rawBaseHash.AddFloat(value, 1000.0f);
    rawBaseHash.AddBool(settings.overrideBlackLevel);
    rawBaseHash.AddFloat(settings.blackLevelOverride, 1000.0f);
    rawBaseHash.AddBool(settings.overrideWhiteLevel);
    rawBaseHash.AddFloat(settings.whiteLevelOverride, 1000.0f);
    rawBaseHash.AddInt(static_cast<int>(settings.highlightMode));
    rawBaseHash.AddFloat(settings.highlightStrength, 1000.0f);
    rawBaseHash.AddFloat(settings.highlightThreshold, 1000.0f);
    rawBaseHash.AddInt(static_cast<int>(settings.demosaicMethod));
    rawBaseHash.AddBool(settings.cameraTransformEnabled);
    rawBaseHash.AddInt(static_cast<int>(settings.cameraTransformSource));
    rawBaseHash.AddBool(settings.debugBypassCameraTransform);
    rawBaseHash.AddBool(settings.debugTransposeCameraMatrix);
    rawBaseHash.AddInt(static_cast<int>(settings.debugView));
    rawBaseHash.AddInt(settings.rotationDegrees);
    rawBaseHash.AddBool(settings.rotateToFitFrame);
    rawBaseHash.AddBool(settings.flipHorizontally);
    rawBaseHash.AddBool(settings.flipVertically);
    rawBaseHash.AddFloat(settings.falseColorSuppression, 1000.0f);
    rawBaseHash.AddFloat(settings.defringeStrength, 1000.0f);
    rawBaseHash.AddFloat(settings.highlightEdgeCleanup, 1000.0f);
    rawBaseHash.AddInt(settings.chromaRadius);
    rawBaseHash.AddFloat(settings.preserveRealColor, 1000.0f);
    rawBaseHash.AddFloat(settings.lateralRedCyan, 1000.0f);
    rawBaseHash.AddFloat(settings.lateralBlueYellow, 1000.0f);
    rawBaseHash.AddBool(settings.mosaicDenoise.enabled);
    rawBaseHash.AddBool(settings.mosaicDenoise.hotPixelSuppression);
    rawBaseHash.AddFloat(settings.mosaicDenoise.hotPixelThreshold, 1000.0f);
    rawBaseHash.AddFloat(settings.mosaicDenoise.lumaStrength, 1000.0f);
    rawBaseHash.AddFloat(settings.mosaicDenoise.chromaStrength, 1000.0f);
    rawBaseHash.AddInt(settings.mosaicDenoise.radius);
    rawBaseHash.AddFloat(settings.mosaicDenoise.edgeProtection, 1000.0f);
    rawBaseHash.AddInt(settings.mosaicDenoise.iterations);
    fingerprints.rawBase = rawBaseHash.hash;

    DevelopAutoStageHashBuilder rawGlobalHash;
    rawGlobalHash.AddValue(fingerprints.rawBase);
    rawGlobalHash.AddFloat(solveGuidance.exposureBias, 1000.0f);
    rawGlobalHash.AddFloat(solveGuidance.highlightGuard, 1000.0f);
    rawGlobalHash.AddFloat(solveGuidance.highlightCharacter, 1000.0f);
    rawGlobalHash.AddFloat(stats.highlightPressure, 1000.0f);
    rawGlobalHash.AddFloat(stats.clippingRatio, 10000.0f);
    fingerprints.rawGlobal = rawGlobalHash.hash;

    DevelopAutoStageHashBuilder scenePrepHash;
    scenePrepHash.AddValue(fingerprints.rawGlobal);
    scenePrepHash.AddBool(prepSettings.autoSafetyEnabled);
    scenePrepHash.AddFloat(prepSettings.minEvBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.maxEvBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.baseEvBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.noiseProtectionBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.highlightProtectionBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.shadowLiftLimitBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.wellExposedTargetBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.minEv, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.maxEv, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.baseEv, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.strength, 1000.0f);
    scenePrepHash.AddInt(prepSettings.sampleCount);
    scenePrepHash.AddFloat(prepSettings.baseRadiusPercent, 100000.0f);
    scenePrepHash.AddFloat(prepSettings.highlightProtection, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.shadowLiftLimit, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.noiseProtection, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.detailWeight, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.wellExposedTarget, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.smoothGradientProtection, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.textureSensitivity, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.skyBias, 1000.0f);
    scenePrepHash.AddInt(prepSettings.smoothnessRadius);
    scenePrepHash.AddInt(prepSettings.smoothAreaRadius);
    scenePrepHash.AddFloat(prepSettings.edgeAwareness, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.haloGuard, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.maskDebandDither, 1000.0f);
    fingerprints.scenePrep = scenePrepHash.hash;

    DevelopAutoStageHashBuilder finishToneHash;
    finishToneHash.AddValue(fingerprints.scenePrep);
    finishToneHash.AddString(EditorNodeGraph::DevelopAutoIntentStableString(solveGuidance.intent));
    finishToneHash.AddFloat(solveGuidance.autoStrength, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.dynamicRange, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.shadowLift, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.highlightGuard, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.highlightCharacter, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.contrastBias, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.subjectSceneBias, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.moodReadabilityBias, 1000.0f);
    finishToneHash.AddString(result.selectedId);
    finishToneHash.AddString(result.selectionSource);
    fingerprints.finishTone = finishToneHash.hash;

    DevelopAutoStageHashBuilder finalHash;
    finalHash.AddValue(fingerprints.finishTone);
    finalHash.AddValue(result.fingerprint);
    finalHash.AddInt(result.convergencePass);
    finalHash.AddBool(result.converged);
    finalHash.AddBool(result.renderedFeedbackApplied);
    finalHash.AddString(result.renderedFeedbackStopReason);
    finalHash.AddString(result.renderedFeedbackRevisionStage);
    fingerprints.finalValidation = finalHash.hash;
    return fingerprints;
}

void WriteDevelopAutoStageSolveDiagnostics(
    nlohmann::json& toneJson,
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings,
    const Raw::RawDetailFusionSettings& prepSettings,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopAutoCandidateSolveResult& result,
    const DevelopToneAutoStats& stats) {
    const nlohmann::json previousFingerprints =
        toneJson.value("autoStageFingerprints", nlohmann::json::object());
    const DevelopAutoStageFingerprints fingerprints =
        BuildDevelopAutoStageFingerprints(
            metadata,
            settings,
            prepSettings,
            solveGuidance,
            result,
            stats);
    const bool awaitingRenderedMetrics =
        toneJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool finalConverged =
        result.converged ||
        (toneJson.value("autoCandidateRenderedConverged", false) &&
         !result.renderedFeedbackApplied);
    std::string earliestStage =
        DevelopAutoStageForChangedFingerprint(previousFingerprints, fingerprints);
    const std::string revisionStage =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackRevisionStage
            : toneJson.value("autoCandidateRenderedRevisionStage", std::string());
    if (!revisionStage.empty() &&
        revisionStage != "none" &&
        revisionStage != "converged") {
        const std::string revisionState =
            DevelopAutoStageStateForRevisionStage(revisionStage);
        if (DevelopAutoStageOrder(revisionState) < DevelopAutoStageOrder(earliestStage) ||
            earliestStage == "CONVERGED") {
            earliestStage = revisionState;
        }
    } else if (finalConverged) {
        earliestStage = "CONVERGED";
    }

    const int stagePass = result.renderedFeedbackPass > 0
        ? result.renderedFeedbackPass
        : std::max(0, result.convergencePass - 1);
    const nlohmann::json fingerprintsJson = {
        { "metadata", fingerprints.metadata },
        { "rawBase", fingerprints.rawBase },
        { "rawGlobal", fingerprints.rawGlobal },
        { "scenePrep", fingerprints.scenePrep },
        { "finishTone", fingerprints.finishTone },
        { "finalValidation", fingerprints.finalValidation }
    };

    auto stageEntry = [&](std::string state,
                          std::string boundary,
                          std::uint64_t fingerprint,
                          std::string reason) {
        nlohmann::json entry;
        entry["state"] = state;
        entry["boundary"] = boundary;
        entry["fingerprint"] = fingerprint;
        entry["status"] = DevelopAutoStageStatusFor(state, earliestStage, finalConverged, awaitingRenderedMetrics);
        entry["reason"] = std::move(reason);
        return entry;
    };

    nlohmann::json stages = nlohmann::json::array({
        stageEntry("NEED_SOURCE", "source", fingerprints.metadata, "Confirm a RAW source and metadata are available."),
        stageEntry("METADATA_BOOTSTRAP", "metadata", fingerprints.metadata, "Use source metadata, black/white levels, and WB anchors as the first solve input."),
        stageEntry("RENDER_RAW_BASE", "rawBase", fingerprints.rawBase, "Render the current RAW base boundary; in this build RAW exposure still lives inside this cache boundary."),
        stageEntry("ANALYZE_RAW_BASE", "rawBase", fingerprints.rawBase, "Analyze RAW/base risk such as clipping, highlight pressure, and noise where current stats are available."),
        stageEntry("SOLVE_GLOBAL", "rawGlobal", fingerprints.rawGlobal, "Solve global RAW exposure, WB, highlight reconstruction, and RAW cleanup intent."),
        stageEntry("RENDER_GLOBAL_BASE", "rawGlobal", fingerprints.rawGlobal, "Validate the global solve before scene-prep decisions consume it."),
        stageEntry("SOLVE_SCENE_PREP", "scenePrep", fingerprints.scenePrep, "Solve local exposure/range, highlight/shadow guard, noise guard, and halo safety."),
        stageEntry("RENDER_PREFINISH", "scenePrep", fingerprints.scenePrep, "Render the hidden pre-finish boundary after RAW and scene prep."),
        stageEntry("ANALYZE_PREFINISH", "scenePrep", fingerprints.scenePrep, "Analyze the pre-finish state so finish tone does not hide upstream mistakes."),
        stageEntry("SOLVE_FINISH_TONE", "finishTone", fingerprints.finishTone, "Solve integrated finish tone and contrast intent from the current pre-finish state."),
        stageEntry("RENDER_FINAL", "finishTone", fingerprints.finishTone, "Render final Develop output before downstream View Transform."),
        stageEntry("VALIDATE_FINAL", "finalValidation", fingerprints.finalValidation, "Validate rendered candidates and decide whether another bounded pass is useful."),
        stageEntry("CONVERGED", "finalValidation", fingerprints.finalValidation, "Stop when the solve is stable or rendered feedback no longer improves the authored state.")
    });

    toneJson["autoStageSolveVersion"] = "StagedAutoSolveV1";
    toneJson["autoStageSolveDescription"] =
        "Logical staged Auto solve record for RAW/base, global, scene-prep, pre-finish, finish-tone, and final validation boundaries.";
    toneJson["autoStagePassBudget"] = {
        { "maxRenderedFeedbackPasses", 3 },
        { "bootstrapPass", 0 },
        { "statsSolvePass", 1 },
        { "refinePass", 2 },
        { "emergencyStabilizationPass", 3 }
    };
    toneJson["autoStageCurrentPass"] = stagePass;
    toneJson["autoStageCurrentPassKind"] = DevelopAutoStagePassKind(stagePass);
    toneJson["autoStageEarliestDirtyStage"] = earliestStage;
    toneJson["autoStageEarliestDirtyBoundary"] =
        earliestStage == "METADATA_BOOTSTRAP" ? "metadata" :
        (earliestStage == "RENDER_RAW_BASE" || earliestStage == "ANALYZE_RAW_BASE" ? "rawBase" :
        (earliestStage == "SOLVE_GLOBAL" || earliestStage == "RENDER_GLOBAL_BASE" ? "rawGlobal" :
        (earliestStage == "SOLVE_SCENE_PREP" || earliestStage == "RENDER_PREFINISH" || earliestStage == "ANALYZE_PREFINISH" ? "scenePrep" :
        (earliestStage == "SOLVE_FINISH_TONE" || earliestStage == "RENDER_FINAL" ? "finishTone" : "finalValidation"))));
    toneJson["autoStageRevisionStage"] = revisionStage.empty() ? "none" : revisionStage;
    toneJson["autoStageResponsibleRevisionState"] =
        DevelopAutoStageStateForRevisionStage(revisionStage.empty() ? "none" : revisionStage);
    toneJson["autoStageRevisionReason"] =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackRevisionReason
            : toneJson.value("autoCandidateRenderedRevisionReason", std::string());
    toneJson["autoStageCurrentRawExposureInsideRawBase"] = true;
    toneJson["autoStageCacheSplitStatus"] =
        "logicalOnlyCurrentRawGpuPipelineStillRendersRawExposureInsideRawBase";
    toneJson["autoStageRenderedMetricsRequired"] = awaitingRenderedMetrics;
    toneJson["autoStageValidationState"] =
        finalConverged ? "converged" : (awaitingRenderedMetrics ? "awaitingRenderedMetrics" : "validatedThisSolve");
    toneJson["autoStageFingerprints"] = fingerprintsJson;
    toneJson["autoStageSolveStages"] = std::move(stages);
}

EditorNodeGraph::DevelopAutoGuidance AdjustDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& base,
    float exposureBias,
    float dynamicRangeBias,
    float shadowLiftBias,
    float highlightGuardBias,
    float highlightCharacterBias,
    float contrastBias) {
    EditorNodeGraph::DevelopAutoGuidance adjusted = base;
    adjusted.exposureBias += exposureBias;
    adjusted.dynamicRange += dynamicRangeBias;
    adjusted.shadowLift += shadowLiftBias;
    adjusted.highlightGuard += highlightGuardBias;
    adjusted.highlightCharacter += highlightCharacterBias;
    adjusted.contrastBias += contrastBias;
    EditorModule::NormalizeDevelopAutoGuidance(adjusted);
    return adjusted;
}

EditorNodeGraph::DevelopAutoGuidance BlendDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b,
    float aWeight) {
    const float safeAWeight = std::clamp(aWeight, 0.0f, 1.0f);
    const float bWeight = 1.0f - safeAWeight;
    EditorNodeGraph::DevelopAutoGuidance blended = a;
    blended.autoStrength = a.autoStrength * safeAWeight + b.autoStrength * bWeight;
    blended.exposureBias = a.exposureBias * safeAWeight + b.exposureBias * bWeight;
    blended.dynamicRange = a.dynamicRange * safeAWeight + b.dynamicRange * bWeight;
    blended.shadowLift = a.shadowLift * safeAWeight + b.shadowLift * bWeight;
    blended.highlightGuard = a.highlightGuard * safeAWeight + b.highlightGuard * bWeight;
    blended.highlightCharacter = a.highlightCharacter * safeAWeight + b.highlightCharacter * bWeight;
    blended.contrastBias = a.contrastBias * safeAWeight + b.contrastBias * bWeight;
    blended.subjectSceneBias = a.subjectSceneBias * safeAWeight + b.subjectSceneBias * bWeight;
    blended.moodReadabilityBias = a.moodReadabilityBias * safeAWeight + b.moodReadabilityBias * bWeight;
    EditorModule::NormalizeDevelopAutoGuidance(blended);
    return blended;
}

EditorNodeGraph::DevelopAutoGuidance BlendDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b,
    const EditorNodeGraph::DevelopAutoGuidance& c,
    float aWeight,
    float bWeight,
    float cWeight) {
    const float safeAWeight = std::max(0.0f, aWeight);
    const float safeBWeight = std::max(0.0f, bWeight);
    const float safeCWeight = std::max(0.0f, cWeight);
    const float weightSum = safeAWeight + safeBWeight + safeCWeight;
    if (weightSum <= 0.0001f) {
        return a;
    }

    const float normalizedA = safeAWeight / weightSum;
    const float normalizedB = safeBWeight / weightSum;
    const float normalizedC = safeCWeight / weightSum;
    EditorNodeGraph::DevelopAutoGuidance blended = a;
    blended.autoStrength =
        a.autoStrength * normalizedA + b.autoStrength * normalizedB + c.autoStrength * normalizedC;
    blended.exposureBias =
        a.exposureBias * normalizedA + b.exposureBias * normalizedB + c.exposureBias * normalizedC;
    blended.dynamicRange =
        a.dynamicRange * normalizedA + b.dynamicRange * normalizedB + c.dynamicRange * normalizedC;
    blended.shadowLift =
        a.shadowLift * normalizedA + b.shadowLift * normalizedB + c.shadowLift * normalizedC;
    blended.highlightGuard =
        a.highlightGuard * normalizedA + b.highlightGuard * normalizedB + c.highlightGuard * normalizedC;
    blended.highlightCharacter =
        a.highlightCharacter * normalizedA + b.highlightCharacter * normalizedB + c.highlightCharacter * normalizedC;
    blended.contrastBias =
        a.contrastBias * normalizedA + b.contrastBias * normalizedB + c.contrastBias * normalizedC;
    blended.subjectSceneBias =
        a.subjectSceneBias * normalizedA + b.subjectSceneBias * normalizedB + c.subjectSceneBias * normalizedC;
    blended.moodReadabilityBias =
        a.moodReadabilityBias * normalizedA + b.moodReadabilityBias * normalizedB + c.moodReadabilityBias * normalizedC;
    EditorModule::NormalizeDevelopAutoGuidance(blended);
    return blended;
}

float DevelopAutoCandidateDistance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b) {
    return
        std::fabs(a.exposureBias - b.exposureBias) * 0.70f +
        std::fabs(a.dynamicRange - b.dynamicRange) * 0.35f +
        std::fabs(a.shadowLift - b.shadowLift) * 0.40f +
        std::fabs(a.highlightGuard - b.highlightGuard) * 0.40f +
        std::fabs(a.highlightCharacter - b.highlightCharacter) * 0.20f +
        std::fabs(a.contrastBias - b.contrastBias) * 0.45f +
        std::fabs(a.subjectSceneBias - b.subjectSceneBias) * 0.18f +
        std::fabs(a.moodReadabilityBias - b.moodReadabilityBias) * 0.18f +
        std::fabs(a.autoStrength - b.autoStrength) * 0.20f;
}

std::string DevelopRenderedRevisionStageForRefineIntent(const std::string& refineIntent) {
    if (refineIntent == "protectHighlights") {
        return "rawGlobal";
    }
    if (refineIntent == "brightenMids" || refineIntent == "openShadows") {
        return "scenePrep";
    }
    if (refineIntent == "addContrast") {
        return "finishTone";
    }
    if (refineIntent == "cleanShadows" || refineIntent == "preserveTexture") {
        return "rawCleanup";
    }
    return "multiStage";
}

bool IsDevelopFinishToneProbeCandidateId(const std::string& candidateId) {
    return
        candidateId == "strongerContrast" ||
        candidateId == "toneSofterRolloff" ||
        candidateId == "naturalContrastGuard" ||
        candidateId == "brightHighlightRolloff" ||
        candidateId == "luminousHighlightAnchor" ||
        candidateId == "specularHighlightTolerance" ||
        candidateId == "tonePunchierShape" ||
        candidateId == "toneFlatterEditing" ||
        candidateId == "toneDarkerToe" ||
        candidateId == "renderedLocalContrastShape";
}

bool IsDevelopSubjectIntentCandidateId(const std::string& candidateId) {
    return
        candidateId == "subjectReadableMids" ||
        candidateId == "sceneMoodPreservation";
}

std::string DevelopRenderedRevisionStageForCandidateId(const std::string& candidateId) {
    if (candidateId == "protectHighlights" ||
        candidateId == "highlightProtectedMids" ||
        candidateId == "renderedLocalHighlightRestraint" ||
        IsDevelopWhiteBalanceProbeCandidateId(candidateId)) {
        return "rawGlobal";
    }
    if (candidateId == "brighterMids" ||
        candidateId == "maximumRange" ||
        candidateId == "broadHighlightGuard" ||
        candidateId == "haloSafeLocalRange" ||
        candidateId == "localRangeGuard" ||
        candidateId == "shadowReadabilityLift" ||
        candidateId == "shadowNoiseFloor" ||
        IsDevelopSubjectIntentCandidateId(candidateId) ||
        candidateId == "renderedLocalBrightenMids" ||
        candidateId == "renderedLocalShadowOpening") {
        return "scenePrep";
    }
    if (IsDevelopFinishToneProbeCandidateId(candidateId)) {
        return "finishTone";
    }
    if (candidateId == "cleanShadows" ||
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture") {
        return "rawCleanup";
    }
    return "multiStage";
}

std::string DevelopRenderedRevisionStageForGuidanceDelta(
    const EditorNodeGraph::DevelopAutoGuidance& from,
    const EditorNodeGraph::DevelopAutoGuidance& to,
    const std::string& candidateId) {
    const std::string explicitStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    if (explicitStage != "multiStage") {
        return explicitStage;
    }

    const float rawGlobalDelta =
        std::fabs(to.exposureBias - from.exposureBias) * 0.80f +
        std::max(0.0f, to.highlightGuard - from.highlightGuard) * 0.45f;
    const float scenePrepDelta =
        std::fabs(to.dynamicRange - from.dynamicRange) * 0.35f +
        std::fabs(to.shadowLift - from.shadowLift) * 0.45f +
        std::fabs(to.highlightGuard - from.highlightGuard) * 0.20f;
    const float finishToneDelta =
        std::fabs(to.contrastBias - from.contrastBias) * 0.55f +
        std::fabs(to.highlightCharacter - from.highlightCharacter) * 0.35f;

    if (rawGlobalDelta < 0.055f &&
        scenePrepDelta < 0.055f &&
        finishToneDelta < 0.055f) {
        return "multiStage";
    }
    if (rawGlobalDelta >= scenePrepDelta && rawGlobalDelta >= finishToneDelta) {
        return "rawGlobal";
    }
    if (scenePrepDelta >= finishToneDelta) {
        return "scenePrep";
    }
    return "finishTone";
}

std::string DevelopRenderedRevisionStageReason(
    const std::string& stage,
    const std::string& fallback) {
    if (stage == "rawGlobal") {
        return "Rendered feedback points to RAW/global placement or highlight reconstruction as the earliest responsible stage.";
    }
    if (stage == "scenePrep") {
        return "Rendered feedback points to scene-prep local exposure/range shaping as the earliest responsible stage.";
    }
    if (stage == "finishTone") {
        return "Rendered feedback points to finish tone or contrast shape as the earliest responsible stage.";
    }
    if (stage == "rawCleanup") {
        return "Rendered feedback points to RAW cleanup, denoise, or texture handling as the earliest responsible stage.";
    }
    return fallback.empty()
        ? "Rendered feedback affects multiple authored stages and must be validated by another rendered pass."
        : fallback;
}

std::uint64_t BuildDevelopAutoCandidateContextFingerprint(
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    const DevelopToneAutoStats& stats) {
    std::uint64_t hash = 1469598103934665603ull;
    auto addValue = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto addString = [&](const std::string& value) {
        for (unsigned char ch : value) {
            addValue(static_cast<std::uint64_t>(ch));
        }
    };
    auto addFloat = [&](float value, float scale) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        addValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(quantized)));
    };

    addString(EditorNodeGraph::DevelopAutoIntentStableString(intent));
    addFloat(guidance.autoStrength, 100.0f);
    addFloat(guidance.exposureBias, 100.0f);
    addFloat(guidance.dynamicRange, 100.0f);
    addFloat(guidance.shadowLift, 100.0f);
    addFloat(guidance.highlightGuard, 100.0f);
    addFloat(guidance.highlightCharacter, 100.0f);
    addFloat(guidance.contrastBias, 100.0f);
    addFloat(guidance.subjectSceneBias, 100.0f);
    addFloat(guidance.moodReadabilityBias, 100.0f);
    addValue(subjectImportance.enabled ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(subjectImportance.regions.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : subjectImportance.regions) {
        addValue(region.enabled ? 1ull : 0ull);
        addValue(static_cast<std::uint64_t>(region.id));
        addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(region.mode));
        addFloat(region.centerX, 1000.0f);
        addFloat(region.centerY, 1000.0f);
        addFloat(region.radiusX, 1000.0f);
        addFloat(region.radiusY, 1000.0f);
        addFloat(region.feather, 1000.0f);
        addFloat(region.strength, 1000.0f);
    }
    addValue(static_cast<std::uint64_t>(subjectImportance.strokes.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : subjectImportance.strokes) {
        addValue(stroke.enabled ? 1ull : 0ull);
        addValue(stroke.subtract ? 1ull : 0ull);
        addValue(static_cast<std::uint64_t>(stroke.id));
        addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(stroke.mode));
        addFloat(stroke.radius, 1000.0f);
        addFloat(stroke.feather, 1000.0f);
        addFloat(stroke.strength, 1000.0f);
        addValue(static_cast<std::uint64_t>(stroke.points.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            addFloat(point.x, 1000.0f);
            addFloat(point.y, 1000.0f);
        }
    }
    addValue(stats.valid ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(std::max(0, stats.sceneProfile)));
    addFloat(stats.shadowPercentile, 200.0f);
    addFloat(stats.midtonePercentile, 200.0f);
    addFloat(stats.highlightPercentile, 200.0f);
    addFloat(stats.clippingRatio, 5000.0f);
    addFloat(stats.highlightPressure, 100.0f);
    addFloat(stats.noiseRisk, 100.0f);
    addFloat(stats.textureConfidence, 100.0f);
    addFloat(stats.hdrSpreadEv, 20.0f);
    return hash;
}

std::uint64_t BuildDevelopAutoCandidateGuidanceFingerprint(
    const std::string& candidateId,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const EditorNodeGraph::DevelopSubjectImportanceMap* subjectImportance = nullptr) {
    std::uint64_t hash = 1469598103934665603ull;
    auto addValue = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto addString = [&](const std::string& value) {
        for (unsigned char ch : value) {
            addValue(static_cast<std::uint64_t>(ch));
        }
    };
    auto addFloat = [&](float value, float scale) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        addValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(quantized)));
    };

    addString(candidateId);
    addString(EditorNodeGraph::DevelopAutoIntentStableString(guidance.intent));
    addFloat(guidance.autoStrength, 100.0f);
    addFloat(guidance.exposureBias, 100.0f);
    addFloat(guidance.dynamicRange, 100.0f);
    addFloat(guidance.shadowLift, 100.0f);
    addFloat(guidance.highlightGuard, 100.0f);
    addFloat(guidance.highlightCharacter, 100.0f);
    addFloat(guidance.contrastBias, 100.0f);
    addFloat(guidance.subjectSceneBias, 100.0f);
    addFloat(guidance.moodReadabilityBias, 100.0f);
    if (subjectImportance) {
        addValue(subjectImportance->enabled ? 1ull : 0ull);
        addValue(static_cast<std::uint64_t>(subjectImportance->regions.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : subjectImportance->regions) {
            addValue(region.enabled ? 1ull : 0ull);
            addValue(static_cast<std::uint64_t>(region.id));
            addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(region.mode));
            addFloat(region.centerX, 1000.0f);
            addFloat(region.centerY, 1000.0f);
            addFloat(region.radiusX, 1000.0f);
            addFloat(region.radiusY, 1000.0f);
            addFloat(region.feather, 1000.0f);
            addFloat(region.strength, 1000.0f);
        }
        addValue(static_cast<std::uint64_t>(subjectImportance->strokes.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : subjectImportance->strokes) {
            addValue(stroke.enabled ? 1ull : 0ull);
            addValue(stroke.subtract ? 1ull : 0ull);
            addValue(static_cast<std::uint64_t>(stroke.id));
            addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(stroke.mode));
            addFloat(stroke.radius, 1000.0f);
            addFloat(stroke.feather, 1000.0f);
            addFloat(stroke.strength, 1000.0f);
            addValue(static_cast<std::uint64_t>(stroke.points.size()));
            for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
                addFloat(point.x, 1000.0f);
                addFloat(point.y, 1000.0f);
            }
        }
    }
    return hash;
}

bool TryReadRememberedCandidateRejection(
    const nlohmann::json& toneJson,
    std::uint64_t candidateContextFingerprint,
    const std::string& candidateId,
    std::string& outReason) {
    const nlohmann::json memory =
        toneJson.value("autoCandidateRejectedMemory", nlohmann::json::array());
    if (!memory.is_array() || candidateContextFingerprint == 0 || candidateId.empty()) {
        return false;
    }

    for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
        if (!it->is_object() ||
            it->value("contextFingerprint", static_cast<std::uint64_t>(0)) != candidateContextFingerprint ||
            it->value("id", std::string()) != candidateId) {
            continue;
        }
        outReason = it->value(
            "reason",
            std::string("Rejected from recent solver memory for this same image/state context."));
        return true;
    }
    return false;
}

bool TryReadRememberedRenderedCandidateRejection(
    const nlohmann::json& toneJson,
    std::uint64_t guidanceFingerprint,
    const std::string& candidateId,
    std::string& outReason) {
    const nlohmann::json memory =
        toneJson.value("autoCandidateRenderedRejectionMemory", nlohmann::json::array());
    if (!memory.is_array() || guidanceFingerprint == 0 || candidateId.empty()) {
        return false;
    }

    for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
        if (!it->is_object() ||
            it->value("guidanceFingerprint", static_cast<std::uint64_t>(0)) != guidanceFingerprint ||
            it->value("id", std::string()) != candidateId) {
            continue;
        }
        outReason = it->value(
            "reason",
            std::string("Rejected from recent rendered candidate memory for this same authored state."));
        return true;
    }
    return false;
}

float ScoreDevelopAutoCandidate(
    const std::string& id,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    const DevelopDynamicRangeStrategy& dynamicRangeStrategy,
    const DevelopSubjectSceneIntent& subjectSceneIntent,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float underBrightBroadHighlightEv) {
    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool cleanIntent = intent == EditorNodeGraph::DevelopAutoIntent::CleanBase;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const float smallSpecularSignal =
        SaturateFloat((0.012f - stats.clippingRatio) / 0.012f) *
        SaturateFloat((0.44f - stats.highlightPressure) / 0.44f) *
        ((!regionEvidence.valid || regionEvidence.smallSpecularLikely) ? 1.0f : 0.45f);
    const float mapHighlightBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapHighlightPriority - 0.5f);
    const float mapShadowBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapShadowVisibility - 0.5f);
    const float mapContrastBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapNaturalContrast - 0.5f);
    const float mapRangeBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapVisibleRange - 0.5f);
    const float subjectPriorityBias =
        std::max(0.0f, subjectSceneIntent.subjectPriority - 0.5f);
    const float subjectReadabilityBias =
        std::max(0.0f, subjectSceneIntent.improveReadability - 0.5f);
    const float subjectMoodBias =
        std::max(0.0f, subjectSceneIntent.preserveMood - 0.5f);
    const float subjectProtectionBias =
        std::max(0.0f, subjectSceneIntent.protectionPressure);
    const float subjectRefinedConfidenceBias =
        SaturateFloat(subjectSceneIntent.refinedMapConfidence + subjectSceneIntent.refinedMapCoverage * 0.28f);
    const float subjectRefinedReadabilityBias =
        SaturateFloat(subjectSceneIntent.refinedMapReadabilityCoverage + subjectRefinedConfidenceBias * 0.20f);
    const float subjectRefinedProtectionBias =
        SaturateFloat(subjectSceneIntent.refinedMapProtectionCoverage + subjectRefinedConfidenceBias * 0.18f);
    const float subjectRefinedMoodBias =
        SaturateFloat(
            subjectSceneIntent.refinedMapMoodCoverage +
            subjectSceneIntent.refinedMapLowPriorityCoverage * 0.30f +
            subjectRefinedConfidenceBias * 0.12f);
    const float broadHighlightSignal = SaturateFloat(
        stats.highlightPressure * 0.50f +
        regionEvidence.broadHighlightPressure * 0.24f +
        regionEvidence.meaningfulHighlightPressure * 0.20f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        stats.clippingRatio * 1.60f -
        smallSpecularSignal * 0.18f);
    const float shadowReadabilitySignal = SaturateFloat(
        shadowRescueNeed * 0.44f +
        regionEvidence.localShadowHotspotRisk * 0.18f +
        SaturateFloat((0.74f - stats.noiseRisk) / 0.74f) * 0.18f +
        stats.textureConfidence * 0.10f -
        regionEvidence.shadowNoiseLiftRisk * 0.18f -
        darkness * (darkIntent ? 0.08f : 0.02f));
    const float highlightBrightnessSignal = SaturateFloat(
        stats.highlightPressure * 0.28f +
        broadHighlightSignal * 0.22f +
        regionEvidence.brightnessHierarchyRisk * 0.20f +
        regionEvidence.flatGrayRisk * 0.10f +
        hdrNeed * 0.08f +
        SaturateFloat((0.020f - stats.clippingRatio) / 0.020f) * 0.08f -
        smallSpecularSignal * 0.06f -
        regionEvidence.localHaloRisk * 0.04f);

    float score = 0.0f;
    if (id == "base") {
        const float stableNatural = SaturateFloat(
            (1.0f - shadowRescueNeed * 0.70f) *
            (1.0f - stats.highlightPressure * 0.55f) *
            (1.0f - stats.noiseRisk * 0.40f) *
            (1.0f - regionEvidence.localRangeConflict * 0.25f) *
            (1.0f - subjectReadabilityBias * 0.12f));
        score = stats.valid ? (0.52f + stableNatural * 0.22f) : 0.70f;
        if (naturalIntent) score += 0.05f;
    } else if (id == "protectHighlights") {
        score =
            0.40f +
            stats.highlightPressure * 0.34f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            stats.clippingRatio * 2.40f +
            hdrNeed * 0.15f +
            mapHighlightBias * 0.08f +
            subjectProtectionBias * 0.08f +
            subjectRefinedProtectionBias * 0.04f;
        if (rangeIntent) score += 0.10f;
        if (brightIntent) score -= 0.05f;
        if (darkIntent) score += 0.05f;
    } else if (id == "highlightProtectedMids") {
        score =
            0.39f +
            stats.highlightPressure * 0.22f +
            hdrNeed * 0.16f +
            shadowRescueNeed * 0.10f +
            regionEvidence.localRangeConflict * 0.07f +
            regionEvidence.localHighlightHotspotRisk * 0.04f +
            mapHighlightBias * 0.04f +
            mapRangeBias * 0.04f +
            subjectPriorityBias * 0.05f +
            subjectReadabilityBias * 0.04f +
            subjectRefinedReadabilityBias * 0.03f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.06f -
            stats.noiseRisk * 0.08f;
        if (rangeIntent) score += 0.10f;
        if (flatIntent) score += 0.06f;
        if (brightIntent) score -= 0.04f;
        if (darkIntent) score += 0.04f;
    } else if (id == "broadHighlightGuard") {
        score =
            0.36f +
            broadHighlightSignal * 0.28f +
            hdrNeed * 0.08f +
            SaturateFloat(stats.clippingRatio / 0.030f) * 0.08f -
            smallSpecularSignal * 0.10f -
            stats.noiseRisk * 0.03f +
            mapHighlightBias * 0.08f +
            mapRangeBias * 0.03f +
            subjectProtectionBias * 0.06f +
            subjectRefinedProtectionBias * 0.04f;
        if (rangeIntent) score += 0.14f;
        if (flatIntent || cleanIntent) score += 0.07f;
        if (naturalIntent) score += 0.04f;
        if (brightIntent) score -= 0.03f;
        if (punchyIntent) score -= 0.05f;
    } else if (id == "brighterMids") {
        score =
            0.38f +
            shadowRescueNeed * 0.30f +
            regionEvidence.localShadowHotspotRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.05f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.16f -
            stats.highlightPressure * 0.16f -
            stats.noiseRisk * 0.10f +
            mapShadowBias * 0.08f +
            subjectReadabilityBias * 0.08f +
            subjectRefinedReadabilityBias * 0.04f;
        if (brightIntent) score += 0.16f;
        if (flatIntent) score += 0.08f;
        if (darkIntent) score -= 0.12f;
    } else if (id == "maximumRange") {
        score =
            0.38f +
            hdrNeed * 0.32f +
            regionEvidence.localRangeConflict * 0.10f +
            regionEvidence.localEvConflict * 0.06f +
            SaturateFloat((base.dynamicRange - 1.0f) / 1.4f) * 0.12f +
            stats.highlightPressure * 0.10f -
            stats.noiseRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.04f +
            mapRangeBias * 0.12f -
            mapContrastBias * 0.04f;
        if (rangeIntent) score += 0.20f;
        if (flatIntent) score += 0.10f;
        if (punchyIntent) score -= 0.06f;
    } else if (id == "shadowReadabilityLift") {
        score =
            0.36f +
            shadowReadabilitySignal * 0.30f +
            shadowRescueNeed * 0.10f +
            regionEvidence.localShadowHotspotRisk * 0.08f -
            stats.noiseRisk * 0.12f -
            regionEvidence.shadowNoiseLiftRisk * 0.12f -
            darkness * 0.04f +
            mapShadowBias * 0.08f +
            subjectReadabilityBias * 0.12f +
            subjectPriorityBias * 0.04f +
            subjectRefinedReadabilityBias * 0.05f;
        if (brightIntent) score += 0.12f;
        if (flatIntent || rangeIntent) score += 0.10f;
        if (naturalIntent) score += 0.05f;
        if (cleanIntent) score += 0.03f;
        if (darkIntent) score -= 0.08f;
        if (punchyIntent) score -= 0.04f;
    } else if (id == "subjectReadableMids") {
        const float userRevealBias = SaturateFloat(
            std::max(0.0f, subjectSceneIntent.userSubjectSceneBias) * 0.46f +
            std::max(0.0f, subjectSceneIntent.userMoodReadabilityBias) * 0.42f +
            subjectSceneIntent.userGuidanceStrength * 0.12f);
        const float subjectRevealSignal = SaturateFloat(
            subjectSceneIntent.subjectPriority * 0.26f +
            subjectSceneIntent.improveReadability * 0.24f +
            subjectSceneIntent.readabilityPressure * 0.14f +
            subjectSceneIntent.automaticConfidence * 0.08f +
            userRevealBias * 0.24f +
            regionEvidence.localShadowHotspotRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.10f -
            regionEvidence.localHaloRisk * 0.05f);
        score =
            0.37f +
            subjectRevealSignal * 0.30f +
            subjectPriorityBias * 0.08f +
            subjectReadabilityBias * 0.12f +
            subjectRefinedReadabilityBias * 0.07f +
            subjectRefinedConfidenceBias * 0.03f +
            shadowReadabilitySignal * 0.08f -
            subjectMoodBias * 0.04f -
            stats.noiseRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.08f -
            regionEvidence.localHaloRisk * 0.04f;
        if (brightIntent) score += 0.10f;
        if (flatIntent || rangeIntent) score += 0.07f;
        if (naturalIntent) score += 0.05f;
        if (darkIntent) score -= 0.05f;
    } else if (id == "sceneMoodPreservation") {
        const float userSceneBias = SaturateFloat(
            std::max(0.0f, -subjectSceneIntent.userSubjectSceneBias) * 0.34f +
            std::max(0.0f, -subjectSceneIntent.userMoodReadabilityBias) * 0.44f +
            subjectSceneIntent.userGuidanceStrength * 0.10f);
        const float sceneMoodSignal = SaturateFloat(
            subjectSceneIntent.sceneIntegrity * 0.24f +
            subjectSceneIntent.preserveMood * 0.26f +
            subjectSceneIntent.moodPreservationPressure * 0.14f +
            darkness * 0.12f +
            stats.noiseRisk * 0.08f +
            userSceneBias * 0.22f -
            subjectReadabilityBias * 0.06f);
        score =
            0.36f +
            sceneMoodSignal * 0.30f +
            subjectMoodBias * 0.12f +
            subjectRefinedMoodBias * 0.06f +
            std::max(0.0f, subjectSceneIntent.sceneIntegrity - 0.5f) * 0.08f +
            regionEvidence.shadowNoiseLiftRisk * 0.06f +
            stats.noiseRisk * 0.04f -
            subjectReadabilityBias * 0.08f;
        if (darkIntent) score += 0.16f;
        if (naturalIntent) score += 0.06f;
        if (cleanIntent) score += 0.03f;
        if (brightIntent) score -= 0.10f;
        if (rangeIntent || flatIntent) score -= 0.04f;
    } else if (id == "preserveMood") {
        score =
            0.36f +
            darkness * 0.25f +
            regionEvidence.shadowNoiseLiftRisk * 0.10f +
            stats.noiseRisk * 0.10f +
            stats.highlightPressure * 0.05f +
            subjectMoodBias * 0.14f;
        if (darkIntent) score += 0.22f;
        if (naturalIntent) score += 0.04f;
        if (brightIntent) score -= 0.12f;
    } else if (id == "naturalContrastGuard") {
        score =
            0.36f +
            flatSceneNeed * 0.18f +
            regionEvidence.flatGrayRisk * 0.18f +
            regionEvidence.brightnessHierarchyRisk * 0.18f +
            stats.textureConfidence * 0.08f -
            hdrNeed * 0.05f -
            stats.highlightPressure * 0.04f -
            regionEvidence.localHaloRisk * 0.04f +
            mapContrastBias * 0.08f -
            mapRangeBias * 0.03f +
            std::max(0.0f, subjectSceneIntent.sceneIntegrity - 0.5f) * 0.06f;
        if (naturalIntent) score += 0.10f;
        if (punchyIntent) score += 0.12f;
        if (darkIntent) score += 0.05f;
        if (brightIntent) score += 0.03f;
        if (rangeIntent) score -= 0.10f;
        if (flatIntent) score -= 0.12f;
    } else if (id == "strongerContrast") {
        score =
            0.36f +
            flatSceneNeed * 0.22f +
            regionEvidence.flatGrayRisk * 0.08f -
            regionEvidence.localHaloRisk * 0.05f +
            (1.0f - SaturateFloat(stats.highlightPressure)) * 0.05f -
            hdrNeed * 0.08f +
            mapContrastBias * 0.06f;
        if (punchyIntent) score += 0.24f;
        if (naturalIntent) score += 0.04f;
        if (rangeIntent || flatIntent) score -= 0.08f;
    } else if (id == "toneSofterRolloff") {
        score =
            0.37f +
            stats.highlightPressure * 0.20f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            hdrNeed * 0.14f +
            stats.clippingRatio * 1.10f -
            flatSceneNeed * 0.03f +
            mapHighlightBias * 0.04f;
        if (rangeIntent || flatIntent) score += 0.08f;
        if (brightIntent) score += 0.04f;
        if (punchyIntent) score -= 0.06f;
    } else if (id == "brightHighlightRolloff") {
        score =
            0.37f +
            stats.highlightPressure * 0.18f +
            hdrNeed * 0.10f +
            flatSceneNeed * 0.04f +
            regionEvidence.brightnessHierarchyRisk * 0.10f +
            regionEvidence.localHighlightHotspotRisk * 0.04f +
            SaturateFloat((0.018f - stats.clippingRatio) / 0.018f) * 0.06f -
            stats.clippingRatio * 0.75f;
        if (naturalIntent) score += 0.06f;
        if (brightIntent || punchyIntent) score += 0.09f;
        if (rangeIntent) score += 0.03f;
        if (darkIntent) score -= 0.03f;
    } else if (id == "luminousHighlightAnchor") {
        score =
            0.36f +
            highlightBrightnessSignal * 0.26f +
            regionEvidence.brightnessHierarchyRisk * 0.08f +
            regionEvidence.broadHighlightPressure * 0.06f +
            stats.textureConfidence * 0.05f -
            stats.clippingRatio * 0.70f -
            regionEvidence.localHaloRisk * 0.04f +
            mapHighlightBias * 0.05f +
            mapContrastBias * 0.03f;
        if (naturalIntent) score += 0.10f;
        if (brightIntent) score += 0.12f;
        if (punchyIntent) score += 0.08f;
        if (rangeIntent) score -= 0.06f;
        if (flatIntent) score -= 0.04f;
        if (darkIntent) score -= 0.03f;
    } else if (id == "specularHighlightTolerance") {
        score =
            0.35f +
            smallSpecularSignal * 0.24f +
            SaturateFloat((0.52f - stats.highlightPressure) / 0.52f) * 0.06f -
            hdrNeed * 0.06f -
            stats.clippingRatio * 0.90f -
            regionEvidence.broadHighlightPressure * 0.10f;
        if (naturalIntent) score += 0.08f;
        if (brightIntent || punchyIntent) score += 0.10f;
        if (rangeIntent || flatIntent) score -= 0.10f;
        if (darkIntent) score -= 0.03f;
    } else if (id == "tonePunchierShape") {
        score =
            0.36f +
            flatSceneNeed * 0.20f +
            regionEvidence.flatGrayRisk * 0.08f +
            stats.textureConfidence * 0.10f -
            stats.highlightPressure * 0.08f -
            hdrNeed * 0.04f +
            mapContrastBias * 0.06f;
        if (punchyIntent) score += 0.22f;
        if (naturalIntent) score += 0.04f;
        if (rangeIntent || flatIntent) score -= 0.08f;
    } else if (id == "toneFlatterEditing") {
        score =
            0.36f +
            hdrNeed * 0.18f +
            shadowRescueNeed * 0.08f +
            regionEvidence.localRangeConflict * 0.08f +
            stats.highlightPressure * 0.04f -
            stats.noiseRisk * 0.04f +
            mapRangeBias * 0.08f +
            subjectReadabilityBias * 0.05f;
        if (flatIntent) score += 0.20f;
        if (rangeIntent) score += 0.10f;
        if (punchyIntent) score -= 0.10f;
    } else if (id == "toneDarkerToe") {
        score =
            0.34f +
            darkness * 0.22f +
            regionEvidence.shadowNoiseLiftRisk * 0.10f +
            stats.noiseRisk * 0.08f +
            stats.highlightPressure * 0.04f +
            subjectMoodBias * 0.12f;
        if (darkIntent) score += 0.22f;
        if (cleanIntent) score += 0.05f;
        if (brightIntent) score -= 0.12f;
    } else if (id == "cleanShadows") {
        score =
            0.34f +
            stats.noiseRisk * 0.34f +
            regionEvidence.shadowNoiseLiftRisk * 0.12f +
            (1.0f - stats.textureConfidence) * 0.12f;
        if (cleanIntent) score += 0.22f;
        if (rangeIntent) score -= 0.05f;
    } else if (id == "preserveTexture") {
        score =
            0.35f +
            stats.textureConfidence * 0.26f +
            SaturateFloat(1.0f - regionEvidence.shadowNoiseLiftRisk) * 0.04f +
            SaturateFloat((0.78f - stats.noiseRisk) / 0.78f) * 0.10f +
            flatSceneNeed * 0.04f;
        if (naturalIntent) score += 0.05f;
        if (punchyIntent) score += 0.08f;
        if (rangeIntent) score += 0.04f;
        if (cleanIntent) score -= 0.08f;
    } else if (id == "localRangeGuard") {
        score =
            0.36f +
            regionEvidence.localRangeConflict * 0.24f +
            regionEvidence.localEvConflict * 0.16f +
            regionEvidence.localHaloRisk * 0.14f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            regionEvidence.localShadowHotspotRisk * 0.06f -
            stats.noiseRisk * 0.04f +
            subjectPriorityBias * 0.07f +
            subjectReadabilityBias * 0.04f +
            subjectProtectionBias * 0.04f;
        if (rangeIntent || flatIntent) score += 0.08f;
        if (punchyIntent) score -= 0.03f;
    } else if (id == "haloSafeLocalRange") {
        score =
            0.36f +
            regionEvidence.localHaloRisk * 0.30f +
            regionEvidence.localRangeConflict * 0.14f +
            regionEvidence.localEvConflict * 0.08f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            regionEvidence.localShadowHotspotRisk * 0.06f +
            hdrNeed * 0.04f -
            stats.noiseRisk * 0.03f +
            subjectPriorityBias * 0.04f;
        if (naturalIntent || cleanIntent) score += 0.08f;
        if (rangeIntent || flatIntent) score += 0.06f;
        if (punchyIntent) score -= 0.04f;
    } else if (id == "shadowNoiseFloor") {
        score =
            0.35f +
            stats.noiseRisk * 0.18f +
            regionEvidence.shadowNoiseLiftRisk * 0.24f +
            regionEvidence.localShadowHotspotRisk * 0.12f +
            darkness * 0.12f -
            shadowRescueNeed * 0.06f -
            stats.textureConfidence * 0.05f +
            subjectMoodBias * 0.10f -
            subjectReadabilityBias * 0.05f;
        if (darkIntent) score += 0.18f;
        if (naturalIntent || cleanIntent) score += 0.06f;
        if (brightIntent) score -= 0.12f;
        if (rangeIntent || flatIntent) score -= 0.08f;
    } else if (id == "wbDaylightCorrection") {
        // Daylight correction tests a plausible technical neutral without erasing all camera mood.
        score =
            0.40f +
            stats.highlightPressure * 0.04f +
            SaturateFloat((0.70f - stats.noiseRisk) / 0.70f) * 0.04f +
            hdrNeed * 0.03f;
        if (cleanIntent || flatIntent || rangeIntent) score += 0.08f;
        if (darkIntent) score -= 0.05f;
    } else if (id == "wbNeutralCorrection") {
        // Neutral correction is most useful for clean/editing bases where color accuracy beats mood.
        score =
            0.38f +
            SaturateFloat((0.72f - stats.noiseRisk) / 0.72f) * 0.05f +
            flatSceneNeed * 0.03f;
        if (cleanIntent || flatIntent) score += 0.11f;
        if (rangeIntent) score += 0.06f;
        if (darkIntent || punchyIntent) score -= 0.05f;
    } else if (id == "wbCameraMood") {
        // Camera mood keeps the recorded illuminant when a correction candidate looks too clinical.
        score =
            0.39f +
            darkness * 0.08f +
            stats.textureConfidence * 0.03f -
            stats.clippingRatio * 0.30f;
        if (naturalIntent || darkIntent) score += 0.08f;
        if (cleanIntent || flatIntent) score -= 0.05f;
    } else if (id == "modeNeighborNaturalMoreRange") {
        score =
            0.42f +
            hdrNeed * 0.22f +
            stats.highlightPressure * 0.06f +
            shadowRescueNeed * 0.04f -
            stats.noiseRisk * 0.05f;
        if (naturalIntent) score += 0.05f;
    } else if (id == "modeNeighborNaturalBrighterMids") {
        score =
            0.38f +
            shadowRescueNeed * 0.24f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.12f -
            stats.highlightPressure * 0.10f -
            stats.noiseRisk * 0.06f +
            subjectReadabilityBias * 0.06f;
        if (naturalIntent || brightIntent) score += 0.05f;
        if (darkIntent) score -= 0.08f;
    } else if (id == "modeNeighborNaturalPunchier") {
        score =
            0.37f +
            flatSceneNeed * 0.18f +
            stats.textureConfidence * 0.06f -
            hdrNeed * 0.06f -
            stats.highlightPressure * 0.05f;
        if (naturalIntent || punchyIntent) score += 0.05f;
        if (rangeIntent || flatIntent) score -= 0.06f;
    } else if (id == "modeNeighborBrightHighlightSafe") {
        score =
            0.40f +
            stats.highlightPressure * 0.24f +
            hdrNeed * 0.10f -
            darkness * 0.04f;
        if (brightIntent) score += 0.08f;
    } else if (id == "modeNeighborDarkReadableMids") {
        score =
            0.39f +
            shadowRescueNeed * 0.20f +
            darkness * 0.12f -
            stats.noiseRisk * 0.06f +
            subjectReadabilityBias * 0.04f +
            subjectMoodBias * 0.04f;
        if (darkIntent) score += 0.10f;
    } else if (id == "modeNeighborPunchySaferRange") {
        score =
            0.39f +
            hdrNeed * 0.16f +
            stats.highlightPressure * 0.10f +
            flatSceneNeed * 0.04f;
        if (punchyIntent) score += 0.10f;
    } else if (id == "modeNeighborRangeNaturalShape") {
        score =
            0.40f +
            flatSceneNeed * 0.13f +
            stats.textureConfidence * 0.06f -
            stats.clippingRatio * 0.80f;
        if (rangeIntent) score += 0.10f;
    } else if (id == "modeNeighborFlatNaturalShape") {
        score =
            0.39f +
            flatSceneNeed * 0.14f +
            stats.textureConfidence * 0.05f -
            stats.highlightPressure * 0.04f;
        if (flatIntent) score += 0.10f;
    } else if (id == "modeNeighborCleanTextureCheck") {
        score =
            0.38f +
            stats.textureConfidence * 0.18f +
            SaturateFloat((0.72f - stats.noiseRisk) / 0.72f) * 0.08f;
        if (cleanIntent || naturalIntent) score += 0.06f;
    } else if (id == "renderedLocalBrightenMids") {
        score =
            0.55f +
            shadowRescueNeed * 0.16f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.10f -
            stats.highlightPressure * 0.10f -
            stats.noiseRisk * 0.06f +
            subjectReadabilityBias * 0.08f;
        if (brightIntent || flatIntent) score += 0.06f;
        if (darkIntent) score -= 0.08f;
    } else if (id == "renderedLocalShadowOpening") {
        score =
            0.56f +
            shadowRescueNeed * 0.18f +
            darkness * 0.08f -
            stats.noiseRisk * 0.08f +
            subjectReadabilityBias * 0.10f;
        if (rangeIntent || flatIntent) score += 0.08f;
        if (darkIntent) score -= 0.05f;
    } else if (id == "renderedLocalHighlightRestraint") {
        score =
            0.56f +
            stats.highlightPressure * 0.16f +
            hdrNeed * 0.12f +
            stats.clippingRatio * 1.40f +
            subjectProtectionBias * 0.08f;
        if (rangeIntent || darkIntent) score += 0.06f;
        if (brightIntent) score -= 0.04f;
    } else if (id == "renderedLocalContrastShape") {
        score =
            0.54f +
            flatSceneNeed * 0.16f +
            (1.0f - SaturateFloat(stats.highlightPressure)) * 0.04f -
            hdrNeed * 0.04f;
        if (punchyIntent) score += 0.12f;
        if (rangeIntent || flatIntent) score -= 0.05f;
    } else if (id == "renderedLocalCleanShadows") {
        score =
            0.55f +
            stats.noiseRisk * 0.20f +
            shadowRescueNeed * 0.06f +
            (1.0f - stats.textureConfidence) * 0.08f;
        if (cleanIntent || darkIntent) score += 0.07f;
        if (rangeIntent) score -= 0.04f;
    } else if (id == "renderedLocalPreserveTexture") {
        score =
            0.54f +
            stats.textureConfidence * 0.18f +
            SaturateFloat((0.70f - stats.noiseRisk) / 0.70f) * 0.08f +
            flatSceneNeed * 0.04f;
        if (naturalIntent || punchyIntent) score += 0.06f;
        if (cleanIntent) score -= 0.06f;
    }
    return SaturateFloat(score);
}

std::string PreferredRenderedRefineCandidateId(const std::string& refineIntent) {
    if (refineIntent == "brightenMids") {
        return "renderedLocalBrightenMids";
    }
    if (refineIntent == "openShadows") {
        return "renderedLocalShadowOpening";
    }
    if (refineIntent == "protectHighlights") {
        return "renderedLocalHighlightRestraint";
    }
    if (refineIntent == "addContrast") {
        return "renderedLocalContrastShape";
    }
    if (refineIntent == "cleanShadows") {
        return "renderedLocalCleanShadows";
    }
    if (refineIntent == "preserveTexture") {
        return "renderedLocalPreserveTexture";
    }
    return {};
}

bool IsRenderedLocalRefineCandidateId(const std::string& candidateId) {
    return
        candidateId == "renderedLocalBrightenMids" ||
        candidateId == "renderedLocalShadowOpening" ||
        candidateId == "renderedLocalHighlightRestraint" ||
        candidateId == "renderedLocalContrastShape" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture";
}

bool IsDevelopCleanupProbeCandidateId(const std::string& candidateId) {
    return
        candidateId == "cleanShadows" ||
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture";
}

bool IsDevelopModeNeighborCandidateId(const std::string& candidateId) {
    return candidateId.rfind("modeNeighbor", 0) == 0;
}

struct DevelopContinuationCandidateBiasProfile {
    bool active = false;
    std::string decision;
    std::string reason;
    std::string stageFocus;
    std::string refineIntent;
};

bool IsActionableDevelopContinuationStage(const std::string& stage) {
    return
        stage == "rawGlobal" ||
        stage == "scenePrep" ||
        stage == "finishTone" ||
        stage == "rawCleanup" ||
        stage == "multiStage";
}

bool DevelopCandidateMatchesRenderedRefineIntent(
    const std::string& candidateId,
    const std::string& refineIntent) {
    if (refineIntent == "brightenMids") {
        return
            candidateId == "brighterMids" ||
            candidateId == "subjectReadableMids" ||
            candidateId == "renderedLocalBrightenMids" ||
            candidateId == "modeNeighborNaturalBrighterMids" ||
            candidateId == "modeNeighborDarkReadableMids";
    }
    if (refineIntent == "openShadows") {
        return
            candidateId == "maximumRange" ||
            candidateId == "haloSafeLocalRange" ||
            candidateId == "localRangeGuard" ||
            candidateId == "shadowReadabilityLift" ||
            candidateId == "shadowNoiseFloor" ||
            candidateId == "subjectReadableMids" ||
            candidateId == "sceneMoodPreservation" ||
            candidateId == "renderedLocalShadowOpening" ||
            candidateId == "modeNeighborNaturalMoreRange" ||
            candidateId == "modeNeighborDarkReadableMids";
    }
    if (refineIntent == "protectHighlights") {
        return
            candidateId == "protectHighlights" ||
            candidateId == "highlightProtectedMids" ||
            candidateId == "broadHighlightGuard" ||
            candidateId == "haloSafeLocalRange" ||
            candidateId == "localRangeGuard" ||
            candidateId == "renderedLocalHighlightRestraint" ||
            candidateId == "toneSofterRolloff" ||
            candidateId == "brightHighlightRolloff" ||
            candidateId == "luminousHighlightAnchor" ||
            candidateId == "specularHighlightTolerance" ||
            candidateId == "modeNeighborNaturalMoreRange" ||
            candidateId == "modeNeighborBrightHighlightSafe" ||
            candidateId == "modeNeighborPunchySaferRange";
    }
    if (refineIntent == "addContrast") {
        return
            candidateId == "strongerContrast" ||
            candidateId == "naturalContrastGuard" ||
            candidateId == "luminousHighlightAnchor" ||
            candidateId == "tonePunchierShape" ||
            candidateId == "renderedLocalContrastShape" ||
            candidateId == "modeNeighborNaturalPunchier" ||
            candidateId == "modeNeighborRangeNaturalShape";
    }
    if (refineIntent == "cleanShadows") {
        return candidateId == "cleanShadows" || candidateId == "renderedLocalCleanShadows" || candidateId == "shadowNoiseFloor";
    }
    if (refineIntent == "preserveTexture") {
        return candidateId == "preserveTexture" || candidateId == "renderedLocalPreserveTexture";
    }
    return false;
}

DevelopContinuationCandidateBiasProfile ResolveDevelopContinuationCandidateBiasProfile(
    const nlohmann::json& previousToneJson) {
    DevelopContinuationCandidateBiasProfile profile;
    const nlohmann::json continuationPolicy =
        previousToneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    if (!continuationPolicy.is_object()) {
        return profile;
    }

    profile.decision = continuationPolicy.value("decision", std::string());
    if (profile.decision != "continue") {
        const bool carryPendingBias =
            profile.decision == "waitForRenderedMetrics" &&
            previousToneJson.value("autoCandidateContinuationBiasActive", false);
        if (!carryPendingBias) {
            return profile;
        }

        // The same bias must remain active while the biased solve is waiting
        // for rendered metrics, otherwise the ready metrics no longer match
        // the candidate-solve fingerprint they were rendered from.
        profile.decision = previousToneJson.value(
            "autoCandidateContinuationBiasDecision",
            std::string("continue"));
        profile.reason = previousToneJson.value(
            "autoCandidateContinuationBiasReason",
            std::string("responsibleStage"));
        profile.stageFocus = previousToneJson.value(
            "autoCandidateContinuationBiasStage",
            std::string());
        profile.refineIntent = previousToneJson.value(
            "autoCandidateContinuationBiasRefineIntent",
            std::string());
        if (!profile.refineIntent.empty() &&
            !IsActionableDevelopContinuationStage(profile.stageFocus)) {
            profile.stageFocus = DevelopRenderedRevisionStageForRefineIntent(profile.refineIntent);
        }
        profile.active = IsActionableDevelopContinuationStage(profile.stageFocus) ||
            !profile.refineIntent.empty();
        return profile;
    }

    profile.stageFocus = continuationPolicy.value(
        "stageFocus",
        previousToneJson.value("autoCandidateRenderedRevisionStage", std::string()));
    if (profile.stageFocus.empty()) {
        profile.stageFocus = previousToneJson.value("autoCandidateRenderedRevisionStage", std::string());
    }
    profile.refineIntent =
        previousToneJson.value("autoCandidateRenderedRefineIntent", std::string());
    if (profile.refineIntent.empty()) {
        profile.refineIntent =
            previousToneJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string());
    }
    if (!profile.refineIntent.empty() &&
        !IsActionableDevelopContinuationStage(profile.stageFocus)) {
        profile.stageFocus = DevelopRenderedRevisionStageForRefineIntent(profile.refineIntent);
    }

    if (!IsActionableDevelopContinuationStage(profile.stageFocus) &&
        profile.refineIntent.empty()) {
        return profile;
    }

    profile.reason = !profile.refineIntent.empty()
        ? std::string("activeRefineIntent")
        : std::string("responsibleStage");
    profile.active = true;
    return profile;
}

float DevelopContinuationCandidateBiasBonus(
    const DevelopContinuationCandidateBiasProfile& profile,
    const std::string& candidateId) {
    if (!profile.active || candidateId == "base") {
        return 0.0f;
    }

    float bonus = 0.0f;
    const std::string preferredRefineCandidate =
        PreferredRenderedRefineCandidateId(profile.refineIntent);
    if (!preferredRefineCandidate.empty() && candidateId == preferredRefineCandidate) {
        // A rendered-local candidate directly answers a measured mismatch from
        // the previous render pass, so it gets the strongest small nudge.
        bonus += 0.090f;
    } else if (!profile.refineIntent.empty() &&
        DevelopCandidateMatchesRenderedRefineIntent(candidateId, profile.refineIntent)) {
        // Generic parameter families can still answer the same visual intent
        // when the exact rendered-local family is unavailable or clustered.
        bonus += 0.060f;
    }

    const std::string candidateStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    if (IsActionableDevelopContinuationStage(profile.stageFocus)) {
        if (candidateStage == profile.stageFocus) {
            bonus += 0.050f;
        } else if (profile.stageFocus == "multiStage" &&
            candidateStage != "multiStage") {
            bonus += 0.025f;
        }
    }

    return std::min(0.120f, bonus);
}

bool ApplyDevelopContinuationCandidateBias(
    DevelopAutoCandidateSolve& candidate,
    const DevelopContinuationCandidateBiasProfile& profile) {
    const float bonus = DevelopContinuationCandidateBiasBonus(profile, candidate.id);
    if (bonus <= 0.0f) {
        return false;
    }

    candidate.continuationBiasActive = true;
    candidate.continuationBiasBonus = bonus;
    candidate.continuationBiasReason = profile.reason;
    candidate.continuationBiasStage = profile.stageFocus;
    candidate.continuationBiasRefineIntent = profile.refineIntent;
    candidate.score = SaturateFloat(candidate.score + bonus);
    return true;
}

float DevelopAutoCandidateModeIntentFit(
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent) {
    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool cleanIntent = intent == EditorNodeGraph::DevelopAutoIntent::CleanBase;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;

    float fit = 0.48f;
    if (candidateId == "base") {
        fit = naturalIntent ? 0.72f : 0.60f;
    } else if (candidateId == "protectHighlights" || candidateId == "renderedLocalHighlightRestraint") {
        fit = 0.56f + (rangeIntent ? 0.20f : 0.0f) + (darkIntent ? 0.08f : 0.0f) - (brightIntent ? 0.06f : 0.0f);
    } else if (candidateId == "highlightProtectedMids") {
        fit = 0.57f + (rangeIntent ? 0.18f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (darkIntent ? 0.06f : 0.0f) - (brightIntent ? 0.05f : 0.0f);
    } else if (candidateId == "broadHighlightGuard") {
        fit = 0.54f + (rangeIntent ? 0.18f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (cleanIntent ? 0.07f : 0.0f) + (naturalIntent ? 0.05f : 0.0f) - (punchyIntent ? 0.06f : 0.0f) - (brightIntent ? 0.03f : 0.0f);
    } else if (candidateId == "brighterMids" || candidateId == "renderedLocalBrightenMids") {
        fit = 0.52f + (brightIntent ? 0.22f : 0.0f) + (flatIntent ? 0.10f : 0.0f) - (darkIntent ? 0.12f : 0.0f);
    } else if (candidateId == "maximumRange") {
        fit = 0.54f + (rangeIntent ? 0.24f : 0.0f) + (flatIntent ? 0.12f : 0.0f) - (punchyIntent ? 0.08f : 0.0f);
    } else if (candidateId == "preserveMood") {
        fit = 0.52f + (darkIntent ? 0.24f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - (brightIntent ? 0.12f : 0.0f);
    } else if (candidateId == "naturalContrastGuard") {
        fit = 0.52f + (naturalIntent ? 0.14f : 0.0f) + (punchyIntent ? 0.16f : 0.0f) + (darkIntent ? 0.06f : 0.0f) + (brightIntent ? 0.04f : 0.0f) - (rangeIntent ? 0.08f : 0.0f) - (flatIntent ? 0.10f : 0.0f);
    } else if (candidateId == "strongerContrast" || candidateId == "renderedLocalContrastShape") {
        fit = 0.50f + (punchyIntent ? 0.26f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - ((rangeIntent || flatIntent) ? 0.08f : 0.0f);
    } else if (candidateId == "toneSofterRolloff") {
        fit = 0.52f + (rangeIntent ? 0.16f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (brightIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.08f : 0.0f);
    } else if (candidateId == "brightHighlightRolloff") {
        fit = 0.54f + (naturalIntent ? 0.10f : 0.0f) + (brightIntent ? 0.14f : 0.0f) + (punchyIntent ? 0.10f : 0.0f) + (rangeIntent ? 0.04f : 0.0f) - (darkIntent ? 0.04f : 0.0f);
    } else if (candidateId == "luminousHighlightAnchor") {
        fit = 0.53f + (naturalIntent ? 0.12f : 0.0f) + (brightIntent ? 0.16f : 0.0f) + (punchyIntent ? 0.10f : 0.0f) - (rangeIntent ? 0.06f : 0.0f) - (flatIntent ? 0.04f : 0.0f) - (darkIntent ? 0.04f : 0.0f);
    } else if (candidateId == "specularHighlightTolerance") {
        fit = 0.52f + (naturalIntent ? 0.12f : 0.0f) + (brightIntent ? 0.14f : 0.0f) + (punchyIntent ? 0.14f : 0.0f) - ((rangeIntent || flatIntent) ? 0.10f : 0.0f) - (darkIntent ? 0.04f : 0.0f);
    } else if (candidateId == "tonePunchierShape") {
        fit = 0.50f + (punchyIntent ? 0.24f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - ((rangeIntent || flatIntent) ? 0.10f : 0.0f);
    } else if (candidateId == "toneFlatterEditing") {
        fit = 0.50f + (flatIntent ? 0.28f : 0.0f) + (rangeIntent ? 0.12f : 0.0f) + (cleanIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.12f : 0.0f);
    } else if (candidateId == "toneDarkerToe") {
        fit = 0.50f + (darkIntent ? 0.26f : 0.0f) + (naturalIntent ? 0.06f : 0.0f) + (cleanIntent ? 0.04f : 0.0f) - (brightIntent ? 0.12f : 0.0f);
    } else if (candidateId == "cleanShadows" || candidateId == "renderedLocalCleanShadows") {
        fit = 0.50f + (cleanIntent ? 0.26f : 0.0f) + (darkIntent ? 0.08f : 0.0f) - (rangeIntent ? 0.06f : 0.0f);
    } else if (candidateId == "preserveTexture" || candidateId == "renderedLocalPreserveTexture") {
        fit = 0.52f + (naturalIntent ? 0.10f : 0.0f) + (punchyIntent ? 0.10f : 0.0f) + (rangeIntent ? 0.06f : 0.0f) - (cleanIntent ? 0.08f : 0.0f);
    } else if (candidateId == "wbDaylightCorrection") {
        fit = 0.52f + (cleanIntent ? 0.12f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (rangeIntent ? 0.08f : 0.0f) - (darkIntent ? 0.06f : 0.0f);
    } else if (candidateId == "wbNeutralCorrection") {
        fit = 0.50f + (cleanIntent ? 0.18f : 0.0f) + (flatIntent ? 0.14f : 0.0f) + (rangeIntent ? 0.06f : 0.0f) - ((darkIntent || punchyIntent) ? 0.06f : 0.0f);
    } else if (candidateId == "wbCameraMood") {
        fit = 0.52f + (naturalIntent ? 0.10f : 0.0f) + (darkIntent ? 0.16f : 0.0f) - ((cleanIntent || flatIntent) ? 0.06f : 0.0f);
    } else if (candidateId == "localRangeGuard") {
        fit = 0.54f + (rangeIntent ? 0.12f : 0.0f) + (flatIntent ? 0.08f : 0.0f) + (naturalIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.04f : 0.0f);
    } else if (candidateId == "haloSafeLocalRange") {
        fit = 0.54f + (naturalIntent ? 0.08f : 0.0f) + (cleanIntent ? 0.08f : 0.0f) + (rangeIntent ? 0.08f : 0.0f) + (flatIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.05f : 0.0f);
    } else if (candidateId == "shadowReadabilityLift") {
        fit = 0.53f + (brightIntent ? 0.14f : 0.0f) + (flatIntent ? 0.12f : 0.0f) + (rangeIntent ? 0.10f : 0.0f) + (naturalIntent ? 0.07f : 0.0f) - (darkIntent ? 0.08f : 0.0f) - (punchyIntent ? 0.04f : 0.0f);
    } else if (candidateId == "subjectReadableMids") {
        fit = 0.54f + (brightIntent ? 0.14f : 0.0f) + (flatIntent ? 0.12f : 0.0f) + (rangeIntent ? 0.08f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - (darkIntent ? 0.06f : 0.0f);
    } else if (candidateId == "sceneMoodPreservation") {
        fit = 0.53f + (darkIntent ? 0.18f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) + (cleanIntent ? 0.06f : 0.0f) - (brightIntent ? 0.10f : 0.0f) - ((rangeIntent || flatIntent) ? 0.06f : 0.0f);
    } else if (candidateId == "shadowNoiseFloor") {
        fit = 0.52f + (darkIntent ? 0.20f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) + (cleanIntent ? 0.08f : 0.0f) - (brightIntent ? 0.12f : 0.0f) - ((rangeIntent || flatIntent) ? 0.08f : 0.0f);
    } else if (IsDevelopModeNeighborCandidateId(candidateId)) {
        fit = 0.62f;
    } else if (IsRenderedLocalRefineCandidateId(candidateId)) {
        fit = 0.58f;
    } else if (candidateId == "mergedAutoPick" ||
        candidateId == "renderedFeedbackMerge" ||
        candidateId == "renderedFeedbackPairMerge" ||
        candidateId == "renderedFeedbackEnsembleMerge") {
        fit = 0.64f;
    }
    return SaturateFloat(fit);
}

nlohmann::json BuildDevelopAutoCandidateScoreComponents(
    const DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    EditorNodeGraph::DevelopAutoIntent intent,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    const DevelopDynamicRangeStrategy& dynamicRangeStrategy,
    const DevelopSubjectSceneIntent& subjectSceneIntent,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float underBrightBroadHighlightEv) {
    const float exposureDelta = candidate.guidance.exposureBias - base.exposureBias;
    const float rangeDelta = candidate.guidance.dynamicRange - base.dynamicRange;
    const float shadowDelta = candidate.guidance.shadowLift - base.shadowLift;
    const float highlightGuardDelta = candidate.guidance.highlightGuard - base.highlightGuard;
    const float highlightCharacterDelta = candidate.guidance.highlightCharacter - base.highlightCharacter;
    const float contrastDelta = candidate.guidance.contrastBias - base.contrastBias;
    const float positiveExposureDelta = std::max(0.0f, exposureDelta);
    const float positiveRangeDelta = std::max(0.0f, rangeDelta);
    const float positiveShadowDelta = std::max(0.0f, shadowDelta);
    const float positiveContrastDelta = std::max(0.0f, contrastDelta);
    const float negativeContrastDelta = std::max(0.0f, -contrastDelta);
    const float underBrightNeed = SaturateFloat(underBrightBroadHighlightEv / 1.25f);
    const float mapHighlightBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapHighlightPriority - 0.5f);
    const float mapShadowBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapShadowVisibility - 0.5f);
    const float mapContrastBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapNaturalContrast - 0.5f);
    const float mapRangeBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapVisibleRange - 0.5f);
    const float subjectPriorityBias =
        std::max(0.0f, subjectSceneIntent.subjectPriority - 0.5f);
    const float subjectReadabilityBias =
        std::max(0.0f, subjectSceneIntent.improveReadability - 0.5f);
    const float subjectMoodBias =
        std::max(0.0f, subjectSceneIntent.preserveMood - 0.5f);
    const float subjectProtectionBias =
        std::max(0.0f, subjectSceneIntent.protectionPressure);
    const float subjectRefinedConfidenceBias =
        SaturateFloat(subjectSceneIntent.refinedMapConfidence + subjectSceneIntent.refinedMapCoverage * 0.28f);
    const float subjectRefinedReadabilityBias =
        SaturateFloat(subjectSceneIntent.refinedMapReadabilityCoverage + subjectRefinedConfidenceBias * 0.20f);
    const float subjectRefinedProtectionBias =
        SaturateFloat(subjectSceneIntent.refinedMapProtectionCoverage + subjectRefinedConfidenceBias * 0.18f);
    const float subjectRefinedMoodBias =
        SaturateFloat(
            subjectSceneIntent.refinedMapMoodCoverage +
            subjectSceneIntent.refinedMapLowPriorityCoverage * 0.30f +
            subjectRefinedConfidenceBias * 0.12f);
    const float smallSpecularSignal =
        SaturateFloat((0.012f - stats.clippingRatio) / 0.012f) *
        SaturateFloat((0.44f - stats.highlightPressure) / 0.44f) *
        ((!regionEvidence.valid || regionEvidence.smallSpecularLikely) ? 1.0f : 0.45f);
    const float broadHighlightSignal = SaturateFloat(
        stats.highlightPressure * 0.50f +
        regionEvidence.broadHighlightPressure * 0.24f +
        regionEvidence.meaningfulHighlightPressure * 0.16f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        stats.clippingRatio * 1.60f -
        smallSpecularSignal * 0.18f);
    const float shadowReadabilitySignal = SaturateFloat(
        shadowRescueNeed * 0.44f +
        regionEvidence.localShadowHotspotRisk * 0.18f +
        SaturateFloat((0.74f - stats.noiseRisk) / 0.74f) * 0.18f +
        stats.textureConfidence * 0.10f -
        regionEvidence.shadowNoiseLiftRisk * 0.18f -
        darkness * 0.04f);
    const float naturalContrastSignal = SaturateFloat(
        flatSceneNeed * 0.28f +
        regionEvidence.flatGrayRisk * 0.26f +
        regionEvidence.brightnessHierarchyRisk * 0.24f +
        regionEvidence.highlightGrayRisk * 0.18f +
        stats.textureConfidence * 0.08f -
        hdrNeed * 0.05f -
        regionEvidence.localHaloRisk * 0.04f);
    const float highlightBrightnessSignal = SaturateFloat(
        stats.highlightPressure * 0.24f +
        broadHighlightSignal * 0.24f +
        regionEvidence.brightnessHierarchyRisk * 0.22f +
        regionEvidence.highlightGrayRisk * 0.20f +
        regionEvidence.flatGrayRisk * 0.10f +
        highlightCharacterDelta * 0.10f +
        positiveContrastDelta * 0.08f -
        stats.clippingRatio * 0.80f -
        smallSpecularSignal * 0.06f);
    const float localHaloSafetySignal = SaturateFloat(
        regionEvidence.localHaloRisk * 0.50f +
        regionEvidence.localRangeConflict * 0.18f +
        regionEvidence.localEvConflict * 0.12f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        regionEvidence.localShadowHotspotRisk * 0.08f);

    const float highlightDamageRisk = SaturateFloat(
        stats.highlightPressure * 0.55f +
        stats.clippingRatio * 2.20f +
        regionEvidence.meaningfulHighlightPressure * 0.10f +
        regionEvidence.localHighlightHotspotRisk * 0.18f +
        positiveExposureDelta * 0.28f -
        std::max(0.0f, highlightGuardDelta) * 0.18f -
        positiveRangeDelta * 0.06f);
    const float shadowNoiseRisk = SaturateFloat(
        stats.noiseRisk * 0.55f +
        regionEvidence.shadowNoiseLiftRisk * 0.22f +
        positiveShadowDelta * 0.18f +
        positiveRangeDelta * 0.08f -
        std::max(0.0f, -shadowDelta) * 0.08f);
    const float flatteningRisk = SaturateFloat(
        positiveRangeDelta * 0.18f +
        negativeContrastDelta * 0.20f +
        hdrNeed * 0.08f +
        regionEvidence.highlightGrayRisk * 0.12f +
        regionEvidence.brightnessHierarchyRisk * 0.14f);

    float midtonePlacement = SaturateFloat(
        0.48f +
        shadowRescueNeed * 0.18f +
        regionEvidence.localShadowHotspotRisk * 0.06f -
        regionEvidence.shadowNoiseLiftRisk * 0.04f +
        underBrightNeed * 0.14f +
        positiveExposureDelta * 0.12f -
        stats.highlightPressure * 0.10f -
        stats.noiseRisk * 0.06f);
    float highlightIntegrity = SaturateFloat(
        0.62f +
        std::max(0.0f, highlightGuardDelta) * 0.20f +
        positiveRangeDelta * 0.08f -
        positiveExposureDelta * 0.16f -
        stats.highlightPressure * 0.20f -
        regionEvidence.localHighlightHotspotRisk * 0.10f -
        stats.clippingRatio * 1.10f);
    float shadowCleanliness = SaturateFloat(
        0.58f -
        stats.noiseRisk * 0.22f -
        positiveShadowDelta * 0.08f +
        std::max(0.0f, -shadowDelta) * 0.06f +
        (candidate.id == "cleanShadows" || candidate.id == "renderedLocalCleanShadows" ? 0.16f : 0.0f));
    float dynamicRangeFit = SaturateFloat(
        0.44f +
        hdrNeed * 0.20f +
        regionEvidence.localRangeConflict * 0.12f +
        regionEvidence.localEvConflict * 0.08f +
        positiveRangeDelta * 0.22f +
        std::max(0.0f, highlightGuardDelta) * 0.08f -
        positiveContrastDelta * 0.06f +
        mapRangeBias * 0.08f);
    float contrastShape = SaturateFloat(
        0.48f +
        flatSceneNeed * 0.14f +
        positiveContrastDelta * 0.18f -
        positiveRangeDelta * 0.08f -
        negativeContrastDelta * 0.04f +
        highlightCharacterDelta * 0.08f +
        mapContrastBias * 0.06f);
    float brightnessHierarchy = SaturateFloat(
        0.54f +
        positiveContrastDelta * 0.08f +
        highlightCharacterDelta * 0.12f -
        positiveRangeDelta * 0.08f -
        negativeContrastDelta * 0.10f -
        flatteningRisk * 0.20f -
        regionEvidence.highlightGrayRisk * 0.08f -
        regionEvidence.brightnessHierarchyRisk * 0.10f);
    float naturalContrastGuard = SaturateFloat(
        0.42f +
        naturalContrastSignal * 0.34f +
        positiveContrastDelta * 0.12f +
        highlightCharacterDelta * 0.10f -
        positiveRangeDelta * 0.06f -
        negativeContrastDelta * 0.08f -
        regionEvidence.localHaloRisk * 0.05f);
    float luminousHighlightAnchor = SaturateFloat(
        0.42f +
        highlightBrightnessSignal * 0.34f +
        positiveContrastDelta * 0.08f +
        highlightCharacterDelta * 0.14f -
        positiveRangeDelta * 0.08f -
        negativeContrastDelta * 0.06f -
        stats.clippingRatio * 0.40f);
    float specularTolerance = SaturateFloat(
        0.42f +
        smallSpecularSignal * 0.36f +
        highlightCharacterDelta * 0.10f +
        positiveContrastDelta * 0.04f -
        stats.highlightPressure * 0.08f -
        regionEvidence.broadHighlightPressure * 0.10f);
    float broadHighlightControl = SaturateFloat(
        0.42f +
        broadHighlightSignal * 0.34f +
        std::max(0.0f, highlightGuardDelta) * 0.12f +
        positiveRangeDelta * 0.06f -
        smallSpecularSignal * 0.10f -
        positiveExposureDelta * 0.06f +
        mapHighlightBias * 0.08f);
    float meaningfulHighlightControl = SaturateFloat(
        0.42f +
        regionEvidence.meaningfulHighlightPressure * 0.34f +
        regionEvidence.highlightStructureScore * 0.12f +
        std::max(0.0f, highlightGuardDelta) * 0.10f +
        positiveRangeDelta * 0.06f -
        smallSpecularSignal * 0.12f -
        positiveExposureDelta * 0.06f);
    float shadowReadabilityLift = SaturateFloat(
        0.42f +
        shadowReadabilitySignal * 0.34f +
        positiveShadowDelta * 0.12f +
        positiveRangeDelta * 0.06f -
        stats.noiseRisk * 0.08f -
        regionEvidence.shadowNoiseLiftRisk * 0.12f +
        mapShadowBias * 0.08f);
    float strategyHighlightFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapHighlightPriority * 0.28f +
        std::max(0.0f, highlightGuardDelta) * 0.10f -
        positiveExposureDelta * 0.06f);
    float strategyShadowFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapShadowVisibility * 0.28f +
        positiveShadowDelta * 0.10f +
        positiveExposureDelta * 0.04f -
        stats.noiseRisk * 0.05f);
    float strategyVisibleRangeFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapVisibleRange * 0.30f +
        positiveRangeDelta * 0.12f +
        std::max(0.0f, highlightGuardDelta) * 0.04f -
        positiveContrastDelta * 0.04f);
    float strategyNaturalContrastFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapNaturalContrast * 0.30f +
        positiveContrastDelta * 0.12f +
        highlightCharacterDelta * 0.06f -
        positiveRangeDelta * 0.05f -
        negativeContrastDelta * 0.04f);
    float noiseTextureQuality = SaturateFloat(
        0.46f +
        stats.textureConfidence * 0.22f -
        stats.noiseRisk * 0.14f +
        (candidate.id == "preserveTexture" || candidate.id == "renderedLocalPreserveTexture" ? 0.16f : 0.0f) +
        (candidate.id == "cleanShadows" || candidate.id == "renderedLocalCleanShadows" ? 0.08f : 0.0f));
    float colorPlausibility = SaturateFloat(
        0.52f +
        (candidate.whiteBalanceProbe ? 0.04f : 0.0f) -
        stats.clippingRatio * 0.20f);
    float moodColorPreservation = SaturateFloat(
        0.54f +
        darkness * 0.08f -
        (candidate.whiteBalanceProbe ? 0.03f : 0.0f));
    float localArtifactSafety = SaturateFloat(
        1.0f - std::max(
            std::max(highlightDamageRisk, flatteningRisk),
            std::max(
                std::max(regionEvidence.localHaloRisk * 0.60f, regionEvidence.localEvConflict * 0.22f),
                regionEvidence.localExposureDamageRisk * 0.70f)));
    float localHaloSafety = SaturateFloat(
        0.44f +
        localHaloSafetySignal * 0.34f +
        std::max(0.0f, highlightGuardDelta) * 0.06f -
        positiveRangeDelta * 0.06f -
        positiveShadowDelta * 0.04f);

    if (candidate.id == "brighterMids" || candidate.id == "renderedLocalBrightenMids") {
        midtonePlacement = SaturateFloat(midtonePlacement + 0.12f);
        strategyShadowFit = SaturateFloat(strategyShadowFit + 0.06f);
    } else if (candidate.id == "maximumRange") {
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.16f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.16f);
    } else if (candidate.id == "broadHighlightGuard") {
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.12f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.08f);
        broadHighlightControl = SaturateFloat(broadHighlightControl + 0.18f);
        meaningfulHighlightControl = SaturateFloat(meaningfulHighlightControl + 0.18f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy - 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.04f);
        strategyHighlightFit = SaturateFloat(strategyHighlightFit + 0.14f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.06f);
    } else if (candidate.id == "preserveMood") {
        midtonePlacement = SaturateFloat(midtonePlacement - 0.06f + darkness * 0.16f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.05f);
    } else if (candidate.id == "naturalContrastGuard") {
        contrastShape = SaturateFloat(contrastShape + 0.14f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.16f);
        naturalContrastGuard = SaturateFloat(naturalContrastGuard + 0.20f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit - 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.02f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.16f);
    } else if (candidate.id == "strongerContrast" || candidate.id == "renderedLocalContrastShape") {
        contrastShape = SaturateFloat(contrastShape + 0.16f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.10f);
    } else if (candidate.id == "toneSofterRolloff") {
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.12f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.06f);
        contrastShape = SaturateFloat(contrastShape - 0.04f);
    } else if (candidate.id == "brightHighlightRolloff") {
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.08f);
        contrastShape = SaturateFloat(contrastShape + 0.06f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.14f);
        strategyHighlightFit = SaturateFloat(strategyHighlightFit + 0.08f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.04f);
    } else if (candidate.id == "luminousHighlightAnchor") {
        // This branch is about perceived light, not clipped-data recovery:
        // keep the pre-finish image stable and test whether downstream
        // shoulder/contrast can stop protected highlights from going gray.
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.06f);
        contrastShape = SaturateFloat(contrastShape + 0.10f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.18f);
        luminousHighlightAnchor = SaturateFloat(luminousHighlightAnchor + 0.22f);
        naturalContrastGuard = SaturateFloat(naturalContrastGuard + 0.06f);
        strategyHighlightFit = SaturateFloat(strategyHighlightFit + 0.08f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.08f);
    } else if (candidate.id == "specularHighlightTolerance") {
        // Tiny glints may stay hot, but this should score as a visible-light
        // hierarchy choice, not as broad highlight recovery.
        highlightIntegrity = SaturateFloat(highlightIntegrity - 0.03f);
        contrastShape = SaturateFloat(contrastShape + 0.08f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.12f);
        specularTolerance = SaturateFloat(specularTolerance + 0.18f);
    } else if (candidate.id == "tonePunchierShape") {
        contrastShape = SaturateFloat(contrastShape + 0.14f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit - 0.04f);
    } else if (candidate.id == "toneFlatterEditing") {
        midtonePlacement = SaturateFloat(midtonePlacement + 0.05f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.12f);
        contrastShape = SaturateFloat(contrastShape - 0.06f);
    } else if (candidate.id == "toneDarkerToe") {
        midtonePlacement = SaturateFloat(midtonePlacement - 0.04f + darkness * 0.08f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.06f);
        contrastShape = SaturateFloat(contrastShape + 0.06f);
    } else if (candidate.id == "localRangeGuard") {
        // Local EV conflict means the solver should test controlled local
        // redistribution, not simply push more global range everywhere.
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.10f + regionEvidence.localEvConflict * 0.08f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.08f);
        localArtifactSafety = SaturateFloat(
            1.0f -
            std::max(
                highlightDamageRisk,
                std::max(
                    flatteningRisk,
                    std::max(regionEvidence.localHaloRisk * 0.80f, regionEvidence.localEvConflict * 0.26f))));
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.05f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.04f);
    } else if (candidate.id == "haloSafeLocalRange") {
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.04f);
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.04f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.12f);
        localHaloSafety = SaturateFloat(localHaloSafety + 0.22f);
        contrastShape = SaturateFloat(contrastShape - 0.02f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.06f);
    } else if (candidate.id == "shadowReadabilityLift") {
        midtonePlacement = SaturateFloat(midtonePlacement + 0.08f);
        shadowReadabilityLift = SaturateFloat(shadowReadabilityLift + 0.18f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.06f);
        shadowCleanliness = SaturateFloat(shadowCleanliness - 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.03f);
        strategyShadowFit = SaturateFloat(strategyShadowFit + 0.16f);
    } else if (candidate.id == "subjectReadableMids") {
        // Guide 05 subject intent is a bias, not a hard mask: test a local
        // readability branch while keeping noise/halo and highlight safeguards visible.
        midtonePlacement = SaturateFloat(midtonePlacement + 0.10f);
        shadowReadabilityLift = SaturateFloat(shadowReadabilityLift + 0.14f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.05f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.04f);
        strategyShadowFit = SaturateFloat(strategyShadowFit + 0.10f);
    } else if (candidate.id == "sceneMoodPreservation") {
        // This is the counter-candidate for silhouettes and low-key scenes:
        // preserve mood/scene hierarchy instead of assuming every likely subject
        // must be lifted toward neutral mids.
        midtonePlacement = SaturateFloat(midtonePlacement - 0.04f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.08f);
        contrastShape = SaturateFloat(contrastShape + 0.05f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.06f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.06f);
    } else if (candidate.id == "shadowNoiseFloor") {
        midtonePlacement = SaturateFloat(midtonePlacement - 0.05f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.14f);
        noiseTextureQuality = SaturateFloat(noiseTextureQuality + 0.08f);
        contrastShape = SaturateFloat(contrastShape + 0.05f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit - 0.04f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.08f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.04f);
    } else if (candidate.id == "wbDaylightCorrection") {
        colorPlausibility = SaturateFloat(colorPlausibility + 0.12f);
        moodColorPreservation = SaturateFloat(moodColorPreservation - 0.03f);
    } else if (candidate.id == "wbNeutralCorrection") {
        colorPlausibility = SaturateFloat(colorPlausibility + 0.14f);
        moodColorPreservation = SaturateFloat(moodColorPreservation - 0.08f);
    } else if (candidate.id == "wbCameraMood") {
        colorPlausibility = SaturateFloat(colorPlausibility + 0.03f);
        moodColorPreservation = SaturateFloat(moodColorPreservation + 0.14f);
    }

    const float modeIntentFit = DevelopAutoCandidateModeIntentFit(candidate.id, intent);
    const float dataRiskPenalty = SaturateFloat(
        highlightDamageRisk * 0.42f +
        shadowNoiseRisk * 0.32f +
        flatteningRisk * 0.22f +
        regionEvidence.localHaloRisk * 0.04f +
        regionEvidence.localEvConflict * 0.04f);

    return {
        { "version", "ParameterScoreComponentsV1" },
        { "scoreSource", "parameterCandidateHeuristic" },
        { "candidateId", candidate.id },
        { "autoIntent", EditorNodeGraph::DevelopAutoIntentStableString(intent) },
        { "dynamicRangeStrategyMap", {
            { "version", kDevelopDynamicRangeStrategyMapVersion },
            { "highlightShadowAxis", dynamicRangeStrategy.strategyMapHighlightShadowAxis },
            { "contrastRangeAxis", dynamicRangeStrategy.strategyMapContrastRangeAxis },
            { "highlightPriority", dynamicRangeStrategy.strategyMapHighlightPriority },
            { "shadowVisibility", dynamicRangeStrategy.strategyMapShadowVisibility },
            { "naturalContrast", dynamicRangeStrategy.strategyMapNaturalContrast },
            { "visibleRange", dynamicRangeStrategy.strategyMapVisibleRange }
        } },
        { "subjectSceneIntent", {
            { "version", kDevelopSubjectSceneIntentVersion },
            { "id", subjectSceneIntent.id },
            { "label", subjectSceneIntent.label },
            { "solveNotesVersion", kDevelopSubjectImportanceSolveNotesVersion },
            { "solveNotes", subjectSceneIntent.solveNotes },
            { "automaticConfidence", subjectSceneIntent.automaticConfidence },
            { "subjectSceneAxis", subjectSceneIntent.subjectSceneAxis },
            { "moodReadabilityAxis", subjectSceneIntent.moodReadabilityAxis },
            { "userGuidanceActive", subjectSceneIntent.userGuidanceActive },
            { "userGuidanceStatus", subjectSceneIntent.userGuidanceStatus },
            { "userSubjectSceneBias", subjectSceneIntent.userSubjectSceneBias },
            { "userMoodReadabilityBias", subjectSceneIntent.userMoodReadabilityBias },
            { "userGuidanceStrength", subjectSceneIntent.userGuidanceStrength },
            { "importanceRegionCount", subjectSceneIntent.importanceRegionCount },
            { "importanceStrokeCount", subjectSceneIntent.importanceStrokeCount },
            { "importanceStrength", subjectSceneIntent.importanceStrength },
            { "importanceImportant", subjectSceneIntent.importanceImportant },
            { "importanceReveal", subjectSceneIntent.importanceReveal },
            { "importanceProtect", subjectSceneIntent.importanceProtect },
            { "importancePreserveMood", subjectSceneIntent.importancePreserveMood },
            { "importanceIgnore", subjectSceneIntent.importanceIgnore },
            { "importanceMap", subjectSceneIntent.importanceMap },
            { "refinedImportanceMap", subjectSceneIntent.refinedImportanceMap },
            { "importanceMapCoverage", subjectSceneIntent.importanceMapCoverage },
            { "importanceMapPositiveCoverage", subjectSceneIntent.importanceMapPositiveCoverage },
            { "importanceMapLowPriorityCoverage", subjectSceneIntent.importanceMapLowPriorityCoverage },
            { "importanceMapPeak", subjectSceneIntent.importanceMapPeak },
            { "importanceMapConfidence", subjectSceneIntent.importanceMapConfidence },
            { "importanceMapCenterBias", subjectSceneIntent.importanceMapCenterBias },
            { "importanceMapEdgeBias", subjectSceneIntent.importanceMapEdgeBias },
            { "refinedMapCoverage", subjectSceneIntent.refinedMapCoverage },
            { "refinedMapLowPriorityCoverage", subjectSceneIntent.refinedMapLowPriorityCoverage },
            { "refinedMapReadabilityCoverage", subjectSceneIntent.refinedMapReadabilityCoverage },
            { "refinedMapProtectionCoverage", subjectSceneIntent.refinedMapProtectionCoverage },
            { "refinedMapMoodCoverage", subjectSceneIntent.refinedMapMoodCoverage },
            { "refinedMapPeak", subjectSceneIntent.refinedMapPeak },
            { "refinedMapConfidence", subjectSceneIntent.refinedMapConfidence },
            { "refinedMapBoundaryHint", subjectSceneIntent.refinedMapBoundaryHint }
        } },
        { "renderedContinuationBias", {
            { "version", kDevelopContinuationCandidateBiasVersion },
            { "active", candidate.continuationBiasActive },
            { "bonus", candidate.continuationBiasBonus },
            { "reason", candidate.continuationBiasReason },
            { "stageFocus", candidate.continuationBiasStage },
            { "refineIntent", candidate.continuationBiasRefineIntent }
        } },
        { "renderedContinuationExpansion", {
            { "version", kDevelopContinuationCandidateExpansionVersion },
            { "active", candidate.continuationExpansionCandidate },
            { "reason", candidate.continuationExpansionReason },
            { "stageFocus", candidate.continuationExpansionStage },
            { "refineIntent", candidate.continuationExpansionRefineIntent }
        } },
        { "finalScore", candidate.score },
        { "statsValid", stats.valid },
        { "signals", {
            { "darkness", darkness },
            { "shadowRescueNeed", shadowRescueNeed },
            { "hdrNeed", hdrNeed },
            { "flatSceneNeed", flatSceneNeed },
            { "underBrightBroadHighlightEv", underBrightBroadHighlightEv },
            { "broadHighlightSignal", broadHighlightSignal },
            { "shadowReadabilitySignal", shadowReadabilitySignal },
            { "naturalContrastSignal", naturalContrastSignal },
            { "highlightBrightnessSignal", highlightBrightnessSignal },
            { "localHaloSafetySignal", localHaloSafetySignal },
            { "strategyMapHighlightBias", mapHighlightBias },
            { "strategyMapShadowBias", mapShadowBias },
            { "strategyMapNaturalContrastBias", mapContrastBias },
            { "strategyMapVisibleRangeBias", mapRangeBias },
            { "subjectPriorityBias", subjectPriorityBias },
            { "subjectReadabilityBias", subjectReadabilityBias },
            { "subjectProtectionBias", subjectProtectionBias },
            { "subjectMoodBias", subjectMoodBias },
            { "subjectCenterPrior", subjectSceneIntent.centerPrior },
            { "subjectAutomaticConfidence", subjectSceneIntent.automaticConfidence },
            { "subjectUserGuidanceStrength", subjectSceneIntent.userGuidanceStrength },
            { "subjectUserSubjectSceneBias", subjectSceneIntent.userSubjectSceneBias },
            { "subjectUserMoodReadabilityBias", subjectSceneIntent.userMoodReadabilityBias },
            { "subjectImportanceRegionCount", subjectSceneIntent.importanceRegionCount },
            { "subjectImportanceStrokeCount", subjectSceneIntent.importanceStrokeCount },
            { "subjectImportanceStrength", subjectSceneIntent.importanceStrength },
            { "subjectImportanceImportant", subjectSceneIntent.importanceImportant },
            { "subjectImportanceReveal", subjectSceneIntent.importanceReveal },
            { "subjectImportanceProtect", subjectSceneIntent.importanceProtect },
            { "subjectImportancePreserveMood", subjectSceneIntent.importancePreserveMood },
            { "subjectImportanceIgnore", subjectSceneIntent.importanceIgnore },
            { "subjectImportanceMapCoverage", subjectSceneIntent.importanceMapCoverage },
            { "subjectImportanceMapPositiveCoverage", subjectSceneIntent.importanceMapPositiveCoverage },
            { "subjectImportanceMapLowPriorityCoverage", subjectSceneIntent.importanceMapLowPriorityCoverage },
            { "subjectImportanceMapRevealCoverage", subjectSceneIntent.importanceMapRevealCoverage },
            { "subjectImportanceMapProtectCoverage", subjectSceneIntent.importanceMapProtectCoverage },
            { "subjectImportanceMapMoodCoverage", subjectSceneIntent.importanceMapMoodCoverage },
            { "subjectImportanceMapPeak", subjectSceneIntent.importanceMapPeak },
            { "subjectImportanceMapConfidence", subjectSceneIntent.importanceMapConfidence },
            { "subjectImportanceMapCenterBias", subjectSceneIntent.importanceMapCenterBias },
            { "subjectImportanceMapEdgeBias", subjectSceneIntent.importanceMapEdgeBias },
            { "subjectRefinedMapCoverage", subjectSceneIntent.refinedMapCoverage },
            { "subjectRefinedMapLowPriorityCoverage", subjectSceneIntent.refinedMapLowPriorityCoverage },
            { "subjectRefinedMapReadabilityCoverage", subjectSceneIntent.refinedMapReadabilityCoverage },
            { "subjectRefinedMapProtectionCoverage", subjectSceneIntent.refinedMapProtectionCoverage },
            { "subjectRefinedMapMoodCoverage", subjectSceneIntent.refinedMapMoodCoverage },
            { "subjectRefinedMapPeak", subjectSceneIntent.refinedMapPeak },
            { "subjectRefinedMapConfidence", subjectSceneIntent.refinedMapConfidence },
            { "subjectRefinedMapBoundaryHint", subjectSceneIntent.refinedMapBoundaryHint },
            { "subjectReadabilityPressure", subjectSceneIntent.readabilityPressure },
            { "subjectProtectionPressure", subjectSceneIntent.protectionPressure },
            { "subjectMoodPreservationPressure", subjectSceneIntent.moodPreservationPressure },
            { "highlightPressure", stats.highlightPressure },
            { "clippingRatio", stats.clippingRatio },
            { "noiseRisk", stats.noiseRisk },
            { "textureConfidence", stats.textureConfidence },
            { "regionEvidenceValid", regionEvidence.valid },
            { "regionEvidenceSource", regionEvidence.source },
            { "localHighlightHotspotRisk", regionEvidence.localHighlightHotspotRisk },
            { "localShadowHotspotRisk", regionEvidence.localShadowHotspotRisk },
            { "shadowNoiseLiftRisk", regionEvidence.shadowNoiseLiftRisk },
            { "localRangeConflict", regionEvidence.localRangeConflict },
            { "localEvSpreadStops", regionEvidence.localEvSpreadStops },
            { "localEvConflict", regionEvidence.localEvConflict },
            { "localHaloRisk", regionEvidence.localHaloRisk },
            { "flatGrayRisk", regionEvidence.flatGrayRisk },
            { "highlightGrayRisk", regionEvidence.highlightGrayRisk },
            { "localExposureHighlightCrowding", regionEvidence.localExposureHighlightCrowding },
            { "localExposureShadowCrowding", regionEvidence.localExposureShadowCrowding },
            { "localExposureHaloStress", regionEvidence.localExposureHaloStress },
            { "localExposureFlatnessRisk", regionEvidence.localExposureFlatnessRisk },
            { "localExposureDamageRisk", regionEvidence.localExposureDamageRisk },
            { "highlightBandFraction", regionEvidence.highlightBandFraction },
            { "highlightMeanLuma", regionEvidence.highlightMeanLuma },
            { "highlightLowSaturationFraction", regionEvidence.highlightLowSaturationFraction },
            { "highlightTileCoverage", regionEvidence.highlightTileCoverage },
            { "highlightStructureScore", regionEvidence.highlightStructureScore },
            { "meaningfulHighlightPressure", regionEvidence.meaningfulHighlightPressure },
            { "regionalBrightnessHierarchyRisk", regionEvidence.brightnessHierarchyRisk },
            { "smallSpecularSignal", smallSpecularSignal }
        } },
        { "guidanceDelta", {
            { "brightnessIntentDelta", exposureDelta },
            { "dynamicRangeDelta", rangeDelta },
            { "shadowLiftDelta", shadowDelta },
            { "highlightGuardDelta", highlightGuardDelta },
            { "highlightCharacterDelta", highlightCharacterDelta },
            { "contrastBiasDelta", contrastDelta }
        } },
        { "dimensions", {
            { "midtonePlacement", midtonePlacement },
            { "highlightIntegrity", highlightIntegrity },
            { "shadowCleanliness", shadowCleanliness },
            { "dynamicRangeFit", dynamicRangeFit },
            { "contrastShape", contrastShape },
            { "brightnessHierarchy", brightnessHierarchy },
            { "naturalContrastGuard", naturalContrastGuard },
            { "luminousHighlightAnchor", luminousHighlightAnchor },
            { "specularTolerance", specularTolerance },
            { "broadHighlightControl", broadHighlightControl },
            { "meaningfulHighlightControl", meaningfulHighlightControl },
            { "shadowReadabilityLift", shadowReadabilityLift },
            { "strategyHighlightFit", strategyHighlightFit },
            { "strategyShadowFit", strategyShadowFit },
            { "strategyVisibleRangeFit", strategyVisibleRangeFit },
            { "strategyNaturalContrastFit", strategyNaturalContrastFit },
            { "subjectPriorityFit", SaturateFloat(
                0.42f + subjectSceneIntent.subjectPriority * 0.28f +
                subjectPriorityBias * 0.16f +
                subjectSceneIntent.importanceMapConfidence * 0.04f +
                subjectRefinedConfidenceBias * 0.05f +
                subjectSceneIntent.importanceMapCenterBias *
                    subjectSceneIntent.importanceMapPositiveCoverage * 0.04f +
                (candidate.id == "shadowReadabilityLift" ||
                 candidate.id == "subjectReadableMids" ||
                 candidate.id == "localRangeGuard" ||
                 candidate.id == "renderedLocalShadowOpening" ||
                 candidate.id == "renderedLocalBrightenMids" ? 0.08f : 0.0f)) },
            { "subjectReadabilityFit", SaturateFloat(
                0.42f + subjectSceneIntent.improveReadability * 0.28f +
                subjectReadabilityBias * 0.18f +
                subjectSceneIntent.importanceMapRevealCoverage * 0.05f +
                subjectRefinedReadabilityBias * 0.06f +
                (candidate.id == "shadowReadabilityLift" ||
                 candidate.id == "subjectReadableMids" ||
                 candidate.id == "brighterMids" ||
                 candidate.id == "renderedLocalShadowOpening" ||
                 candidate.id == "renderedLocalBrightenMids" ? 0.10f : 0.0f) -
                regionEvidence.shadowNoiseLiftRisk * 0.08f) },
            { "subjectProtectionFit", SaturateFloat(
                0.42f + subjectSceneIntent.protectionPressure * 0.34f +
                subjectProtectionBias * 0.14f +
                subjectSceneIntent.importanceMapProtectCoverage * 0.05f +
                subjectRefinedProtectionBias * 0.05f +
                (candidate.id == "protectHighlights" ||
                 candidate.id == "broadHighlightGuard" ||
                 candidate.id == "renderedLocalHighlightRestraint" ? 0.10f : 0.0f) -
                regionEvidence.localExposureHaloStress * 0.05f) },
            { "subjectMoodFit", SaturateFloat(
                0.42f + subjectSceneIntent.preserveMood * 0.26f +
                subjectMoodBias * 0.18f +
                subjectSceneIntent.importanceMapMoodCoverage * 0.05f +
                subjectSceneIntent.importanceMapLowPriorityCoverage * 0.03f +
                subjectRefinedMoodBias * 0.05f +
                (candidate.id == "preserveMood" ||
                 candidate.id == "sceneMoodPreservation" ||
                 candidate.id == "toneDarkerToe" ||
                 candidate.id == "shadowNoiseFloor" ? 0.10f : 0.0f) -
                subjectReadabilityBias * 0.06f) },
            { "noiseTextureQuality", noiseTextureQuality },
            { "colorPlausibility", colorPlausibility },
            { "moodColorPreservation", moodColorPreservation },
            { "localArtifactSafety", localArtifactSafety },
            { "localHaloSafety", localHaloSafety },
            { "localExposureDamageSafety", SaturateFloat(1.0f - regionEvidence.localExposureDamageRisk) },
            { "modeIntentFit", modeIntentFit },
            { "renderedContinuationFit", candidate.continuationBiasActive
                ? SaturateFloat(0.50f + candidate.continuationBiasBonus / 0.24f)
                : 0.50f },
            { "renderedContinuationCoverage", candidate.continuationExpansionCandidate ? 1.0f : 0.50f },
            { "candidateUniqueness", 1.0f }
        } },
        { "risks", {
            { "highlightDamageRisk", highlightDamageRisk },
            { "shadowNoiseRisk", shadowNoiseRisk },
            { "flatteningRisk", flatteningRisk },
            { "localHaloRisk", regionEvidence.localHaloRisk },
            { "localRangeConflict", regionEvidence.localRangeConflict },
            { "localEvConflict", regionEvidence.localEvConflict },
            { "localExposureDamageRisk", regionEvidence.localExposureDamageRisk },
            { "localExposureHaloStress", regionEvidence.localExposureHaloStress },
            { "subjectOverLiftRisk", SaturateFloat(
                subjectMoodBias * 0.44f +
                regionEvidence.shadowNoiseLiftRisk * 0.26f +
                positiveShadowDelta * 0.16f +
                positiveExposureDelta * 0.12f) },
            { "subjectProtectionTradeoffRisk", SaturateFloat(
                subjectProtectionBias * 0.42f +
                positiveExposureDelta * 0.22f +
                stats.clippingRatio * 1.20f -
                std::max(0.0f, highlightGuardDelta) * 0.12f) },
            { "dataRiskPenalty", dataRiskPenalty }
        } }
    };
}

nlohmann::json BuildFallbackDevelopAutoCandidateScoreComponents(
    const DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base) {
    const float distance = DevelopAutoCandidateDistance(candidate.guidance, base);
    return {
        { "version", "ParameterScoreComponentsV1" },
        { "scoreSource", "authoredStateFallback" },
        { "candidateId", candidate.id },
        { "autoIntent", EditorNodeGraph::DevelopAutoIntentStableString(candidate.guidance.intent) },
        { "renderedContinuationBias", {
            { "version", kDevelopContinuationCandidateBiasVersion },
            { "active", candidate.continuationBiasActive },
            { "bonus", candidate.continuationBiasBonus },
            { "reason", candidate.continuationBiasReason },
            { "stageFocus", candidate.continuationBiasStage },
            { "refineIntent", candidate.continuationBiasRefineIntent }
        } },
        { "renderedContinuationExpansion", {
            { "version", kDevelopContinuationCandidateExpansionVersion },
            { "active", candidate.continuationExpansionCandidate },
            { "reason", candidate.continuationExpansionReason },
            { "stageFocus", candidate.continuationExpansionStage },
            { "refineIntent", candidate.continuationExpansionRefineIntent }
        } },
        { "finalScore", candidate.score },
        { "statsValid", false },
        { "guidanceDistanceFromBase", distance },
        { "dimensions", {
            { "midtonePlacement", 0.50f },
            { "highlightIntegrity", 0.50f },
            { "shadowCleanliness", 0.50f },
            { "dynamicRangeFit", 0.50f },
            { "contrastShape", 0.50f },
            { "brightnessHierarchy", candidate.id == "brightHighlightRolloff" || candidate.id == "luminousHighlightAnchor" || candidate.id == "specularHighlightTolerance" || candidate.id == "naturalContrastGuard" ? 0.64f : 0.50f },
            { "naturalContrastGuard", candidate.id == "naturalContrastGuard" ? 0.70f : 0.50f },
            { "luminousHighlightAnchor", candidate.id == "luminousHighlightAnchor" ? 0.72f : 0.50f },
            { "specularTolerance", candidate.id == "specularHighlightTolerance" ? 0.70f : 0.50f },
            { "broadHighlightControl", candidate.id == "broadHighlightGuard" ? 0.70f : 0.50f },
            { "shadowReadabilityLift", candidate.id == "shadowReadabilityLift" ? 0.70f : 0.50f },
            { "noiseTextureQuality", 0.50f },
            { "colorPlausibility", candidate.whiteBalanceProbe ? 0.58f : 0.50f },
            { "moodColorPreservation", candidate.id == "wbCameraMood" ? 0.64f : 0.50f },
            { "localArtifactSafety", 0.50f },
            { "localHaloSafety", candidate.id == "haloSafeLocalRange" ? 0.70f : 0.50f },
            { "modeIntentFit", DevelopAutoCandidateModeIntentFit(candidate.id, candidate.guidance.intent) },
            { "renderedContinuationFit", candidate.continuationBiasActive
                ? SaturateFloat(0.50f + candidate.continuationBiasBonus / 0.24f)
                : 0.50f },
            { "renderedContinuationCoverage", candidate.continuationExpansionCandidate ? 1.0f : 0.50f },
            { "candidateUniqueness", 1.0f }
        } },
        { "risks", {
            { "highlightDamageRisk", 0.0f },
            { "shadowNoiseRisk", 0.0f },
            { "flatteningRisk", 0.0f },
            { "dataRiskPenalty", 0.0f }
        } }
    };
}

float DevelopAutoCandidateNearestSurvivorDistance(
    const DevelopAutoCandidateSolveResult& result,
    std::size_t candidateIndex) {
    if (candidateIndex >= result.candidates.size()) {
        return 0.0f;
    }
    const DevelopAutoCandidateSolve& candidate = result.candidates[candidateIndex];
    bool compared = false;
    float nearest = 4.0f;
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        if (i == candidateIndex || result.candidates[i].rejected) {
            continue;
        }
        compared = true;
        nearest = std::min(
            nearest,
            DevelopAutoCandidateDistance(candidate.guidance, result.candidates[i].guidance));
    }
    return compared ? nearest : 1.0f;
}

bool RejectDevelopAutoCandidateForDamage(
    DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    const DevelopToneAutoStats& stats,
    EditorNodeGraph::DevelopAutoIntent intent) {
    if (candidate.id == "base" || !stats.valid) {
        return false;
    }

    const float exposureDelta = candidate.guidance.exposureBias - base.exposureBias;
    const float shadowDelta = candidate.guidance.shadowLift - base.shadowLift;
    const float rangeDelta = candidate.guidance.dynamicRange - base.dynamicRange;
    const float contrastDelta = candidate.guidance.contrastBias - base.contrastBias;
    const float highlightGuardDelta = candidate.guidance.highlightGuard - base.highlightGuard;

    const float highlightDamageRisk =
        stats.highlightPressure +
        stats.clippingRatio * 3.0f +
        std::max(0.0f, exposureDelta) * 0.42f -
        std::max(0.0f, highlightGuardDelta) * 0.30f -
        std::max(0.0f, rangeDelta) * 0.08f;
    if (highlightDamageRisk > 1.10f) {
        candidate.rejected = true;
        candidate.rejectReason = "Rejected because the brighter placement risks new broad highlight damage.";
        return true;
    }

    const float shadowNoiseRisk =
        stats.noiseRisk +
        std::max(0.0f, shadowDelta) * 0.42f +
        std::max(0.0f, rangeDelta) * 0.12f -
        std::max(0.0f, -shadowDelta) * 0.12f;
    if (shadowNoiseRisk > 1.12f) {
        candidate.rejected = true;
        candidate.rejectReason = "Rejected because extra shadow lift is likely to reveal noisy shadows.";
        return true;
    }

    const bool flatteningAllowed =
        intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase ||
        intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const float flatteningRisk =
        std::max(0.0f, rangeDelta) * 0.22f +
        std::max(0.0f, -contrastDelta) * 0.22f +
        stats.hdrSpreadEv * 0.01f -
        (flatteningAllowed ? 0.18f : 0.0f);
    if (flatteningRisk > 0.44f && candidate.score < 0.54f) {
        candidate.rejected = true;
        candidate.rejectReason = "Rejected because the range move is likely to look washed out for this intent.";
        return true;
    }

    return false;
}

std::uint64_t BuildDevelopAutoCandidateFingerprint(
    const DevelopAutoCandidateSolveResult& result,
    const DevelopToneAutoStats& stats) {
    std::uint64_t hash = 1469598103934665603ull;
    auto addValue = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    for (unsigned char ch : result.selectedId) {
        addValue(static_cast<std::uint64_t>(ch));
    }
    auto addFloat = [&](float value, float scale) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        addValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(quantized)));
    };
    addFloat(result.authoredGuidance.autoStrength, 100.0f);
    addFloat(result.authoredGuidance.exposureBias, 100.0f);
    addFloat(result.authoredGuidance.dynamicRange, 100.0f);
    addFloat(result.authoredGuidance.shadowLift, 100.0f);
    addFloat(result.authoredGuidance.highlightGuard, 100.0f);
    addFloat(result.authoredGuidance.highlightCharacter, 100.0f);
    addFloat(result.authoredGuidance.contrastBias, 100.0f);
    addFloat(result.authoredGuidance.subjectSceneBias, 100.0f);
    addFloat(result.authoredGuidance.moodReadabilityBias, 100.0f);
    addValue(result.authoredWhiteBalanceProbe ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(result.authoredWhiteBalanceMode));
    addValue(stats.valid ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(std::max(0, stats.sceneProfile)));
    addFloat(stats.highlightPressure, 100.0f);
    addFloat(stats.noiseRisk, 100.0f);
    addFloat(stats.hdrSpreadEv, 20.0f);
    return hash;
}

bool TryReadRenderedCandidateScore(
    const nlohmann::json& renderedSolves,
    const std::string& candidateId,
    float& outScore) {
    if (!renderedSolves.is_array() || candidateId.empty()) {
        return false;
    }

    for (const nlohmann::json& rendered : renderedSolves) {
        if (!rendered.is_object() ||
            rendered.value("id", std::string()) != candidateId ||
            !rendered.value("success", false)) {
            continue;
        }
        outScore = rendered.value("renderScore", -1.0f);
        return outScore >= 0.0f;
    }
    return false;
}

void ReadDevelopMetricArray(
    const nlohmann::json& value,
    const char* key,
    std::array<float, 9>& outValues) {
    const nlohmann::json items = value.value(key, nlohmann::json::array());
    if (!items.is_array()) {
        return;
    }
    const std::size_t count = std::min<std::size_t>(items.size(), outValues.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (items[i].is_number()) {
            outValues[i] = items[i].get<float>();
        }
    }
}

bool ReadDevelopRenderedMetricsFromJson(
    const nlohmann::json& value,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics) {
    if (!value.is_object()) {
        return false;
    }

    outMetrics.meanLuma = value.value("meanLuma", outMetrics.meanLuma);
    outMetrics.medianLuma = value.value("medianLuma", outMetrics.medianLuma);
    outMetrics.p10Luma = value.value("p10Luma", outMetrics.p10Luma);
    outMetrics.p90Luma = value.value("p90Luma", outMetrics.p90Luma);
    outMetrics.shadowFraction = value.value("shadowFraction", outMetrics.shadowFraction);
    outMetrics.highlightFraction = value.value("highlightFraction", outMetrics.highlightFraction);
    outMetrics.clippedFraction = value.value("clippedFraction", outMetrics.clippedFraction);
    outMetrics.contrastSpan = value.value("contrastSpan", outMetrics.contrastSpan);
    outMetrics.meanRed = value.value("meanRed", outMetrics.meanRed);
    outMetrics.meanGreen = value.value("meanGreen", outMetrics.meanGreen);
    outMetrics.meanBlue = value.value("meanBlue", outMetrics.meanBlue);
    outMetrics.warmCoolBias = value.value("warmCoolBias", outMetrics.warmCoolBias);
    outMetrics.magentaGreenBias = value.value("magentaGreenBias", outMetrics.magentaGreenBias);
    outMetrics.channelImbalance = value.value("channelImbalance", outMetrics.channelImbalance);
    outMetrics.colorCastRisk = value.value("colorCastRisk", outMetrics.colorCastRisk);
    outMetrics.meanSaturation = value.value("meanSaturation", outMetrics.meanSaturation);
    outMetrics.lowSaturationFraction = value.value("lowSaturationFraction", outMetrics.lowSaturationFraction);
    outMetrics.highlightBandFraction = value.value("highlightBandFraction", outMetrics.highlightBandFraction);
    outMetrics.highlightMeanLuma = value.value("highlightMeanLuma", outMetrics.highlightMeanLuma);
    outMetrics.highlightLowSaturationFraction =
        value.value("highlightLowSaturationFraction", outMetrics.highlightLowSaturationFraction);
    outMetrics.highlightGrayRisk = value.value("highlightGrayRisk", outMetrics.highlightGrayRisk);
    outMetrics.highlightTileCoverage = value.value("highlightTileCoverage", outMetrics.highlightTileCoverage);
    outMetrics.highlightStructureScore = value.value("highlightStructureScore", outMetrics.highlightStructureScore);
    outMetrics.meaningfulHighlightPressure =
        value.value("meaningfulHighlightPressure", outMetrics.meaningfulHighlightPressure);
    outMetrics.edgeContrast = value.value("edgeContrast", outMetrics.edgeContrast);
    outMetrics.haloRiskFraction = value.value("haloRiskFraction", outMetrics.haloRiskFraction);
    outMetrics.shadowTextureRisk = value.value("shadowTextureRisk", outMetrics.shadowTextureRisk);
    ReadDevelopMetricArray(value, "localMeanLuma3x3", outMetrics.localMeanLuma);
    ReadDevelopMetricArray(value, "localContrastSpan3x3", outMetrics.localContrastSpan);
    ReadDevelopMetricArray(value, "localDamageRiskScore3x3", outMetrics.localDamageRiskScore);
    outMetrics.localLumaSpread = value.value("localLumaSpread", outMetrics.localLumaSpread);
    outMetrics.localEvSpreadStops = value.value("localEvSpreadStops", outMetrics.localEvSpreadStops);
    outMetrics.localEvConflict = value.value("localEvConflict", outMetrics.localEvConflict);
    outMetrics.localContrastPeak = value.value("localContrastPeak", outMetrics.localContrastPeak);
    outMetrics.localShadowPressure = value.value("localShadowPressure", outMetrics.localShadowPressure);
    outMetrics.localHighlightPressure = value.value("localHighlightPressure", outMetrics.localHighlightPressure);
    outMetrics.localDamageRiskMean = value.value("localDamageRiskMean", outMetrics.localDamageRiskMean);
    outMetrics.localDamageRiskPeak = value.value("localDamageRiskPeak", outMetrics.localDamageRiskPeak);
    outMetrics.localDamageRiskPeakTile = value.value("localDamageRiskPeakTile", outMetrics.localDamageRiskPeakTile);
    outMetrics.localExposureHighlightCrowding =
        value.value("localExposureHighlightCrowding", outMetrics.localExposureHighlightCrowding);
    outMetrics.localExposureShadowCrowding =
        value.value("localExposureShadowCrowding", outMetrics.localExposureShadowCrowding);
    outMetrics.localExposureHaloStress =
        value.value("localExposureHaloStress", outMetrics.localExposureHaloStress);
    outMetrics.localExposureFlatnessRisk =
        value.value("localExposureFlatnessRisk", outMetrics.localExposureFlatnessRisk);
    outMetrics.localExposureDamageRisk =
        value.value("localExposureDamageRisk", outMetrics.localExposureDamageRisk);
    outMetrics.subjectCenterPrior = value.value("subjectCenterPrior", outMetrics.subjectCenterPrior);
    outMetrics.subjectReadabilityPressure =
        value.value("subjectReadabilityPressure", outMetrics.subjectReadabilityPressure);
    outMetrics.subjectProtectionPressure =
        value.value("subjectProtectionPressure", outMetrics.subjectProtectionPressure);
    outMetrics.subjectMoodPreservationPressure =
        value.value("subjectMoodPreservationPressure", outMetrics.subjectMoodPreservationPressure);
    outMetrics.subjectImportanceConfidence =
        value.value("subjectImportanceConfidence", outMetrics.subjectImportanceConfidence);
    outMetrics.centerMeanLuma = value.value("centerMeanLuma", outMetrics.centerMeanLuma);
    outMetrics.centerShadowFraction = value.value("centerShadowFraction", outMetrics.centerShadowFraction);
    outMetrics.centerHighlightFraction = value.value("centerHighlightFraction", outMetrics.centerHighlightFraction);
    outMetrics.subjectMarkedSampleCount =
        value.value("subjectMarkedSampleCount", outMetrics.subjectMarkedSampleCount);
    outMetrics.subjectMarkedCoverage = value.value("subjectMarkedCoverage", outMetrics.subjectMarkedCoverage);
    outMetrics.subjectMarkedPositiveCoverage =
        value.value("subjectMarkedPositiveCoverage", outMetrics.subjectMarkedPositiveCoverage);
    outMetrics.subjectMarkedRevealCoverage =
        value.value("subjectMarkedRevealCoverage", outMetrics.subjectMarkedRevealCoverage);
    outMetrics.subjectMarkedProtectCoverage =
        value.value("subjectMarkedProtectCoverage", outMetrics.subjectMarkedProtectCoverage);
    outMetrics.subjectMarkedMoodCoverage =
        value.value("subjectMarkedMoodCoverage", outMetrics.subjectMarkedMoodCoverage);
    outMetrics.subjectMarkedLowPriorityCoverage =
        value.value("subjectMarkedLowPriorityCoverage", outMetrics.subjectMarkedLowPriorityCoverage);
    outMetrics.subjectMarkedMeanLuma =
        value.value("subjectMarkedMeanLuma", outMetrics.subjectMarkedMeanLuma);
    outMetrics.subjectMarkedShadowFraction =
        value.value("subjectMarkedShadowFraction", outMetrics.subjectMarkedShadowFraction);
    outMetrics.subjectMarkedHighlightFraction =
        value.value("subjectMarkedHighlightFraction", outMetrics.subjectMarkedHighlightFraction);
    outMetrics.subjectMarkedClippedFraction =
        value.value("subjectMarkedClippedFraction", outMetrics.subjectMarkedClippedFraction);
    outMetrics.subjectMarkedContrastSpan =
        value.value("subjectMarkedContrastSpan", outMetrics.subjectMarkedContrastSpan);
    outMetrics.subjectMarkedReadabilityScore =
        value.value("subjectMarkedReadabilityScore", outMetrics.subjectMarkedReadabilityScore);
    outMetrics.subjectMarkedProtectionRisk =
        value.value("subjectMarkedProtectionRisk", outMetrics.subjectMarkedProtectionRisk);
    outMetrics.subjectMarkedMoodPreservationScore =
        value.value("subjectMarkedMoodPreservationScore", outMetrics.subjectMarkedMoodPreservationScore);
    outMetrics.subjectMarkedLowPriorityMeanLuma =
        value.value("subjectMarkedLowPriorityMeanLuma", outMetrics.subjectMarkedLowPriorityMeanLuma);
    outMetrics.subjectMarkedLowPriorityBrightFraction =
        value.value("subjectMarkedLowPriorityBrightFraction", outMetrics.subjectMarkedLowPriorityBrightFraction);
    outMetrics.subjectMarkedLowPriorityPressure =
        value.value("subjectMarkedLowPriorityPressure", outMetrics.subjectMarkedLowPriorityPressure);
    return true;
}

bool TryReadRenderedCandidateMetrics(
    const nlohmann::json& renderedSolves,
    const std::string& candidateId,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore) {
    if (!renderedSolves.is_array() || candidateId.empty()) {
        return false;
    }

    for (const nlohmann::json& rendered : renderedSolves) {
        if (!rendered.is_object() ||
            rendered.value("id", std::string()) != candidateId ||
            !rendered.value("success", false)) {
            continue;
        }
        if (!ReadDevelopRenderedMetricsFromJson(rendered.value("metrics", nlohmann::json::object()), outMetrics)) {
            return false;
        }
        outScore = rendered.value("renderScore", -1.0f);
        return outScore >= 0.0f;
    }
    return false;
}

DevelopDynamicRangeRegionEvidence BuildDevelopDynamicRangeRegionEvidenceFromMetrics(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    const std::string& source,
    const std::string& candidateId,
    float renderScore) {
    DevelopDynamicRangeRegionEvidence evidence;
    evidence.valid = true;
    evidence.source = source;
    evidence.candidateId = candidateId;
    evidence.renderScore = renderScore;
    evidence.localHighlightPressure = SaturateFloat(metrics.localHighlightPressure);
    evidence.localShadowPressure = SaturateFloat(metrics.localShadowPressure);
    evidence.localDamageRiskMean = SaturateFloat(metrics.localDamageRiskMean);
    evidence.localDamageRiskPeak = SaturateFloat(metrics.localDamageRiskPeak);
    evidence.localLumaSpread = SaturateFloat(metrics.localLumaSpread);
    evidence.localEvSpreadStops = std::clamp(metrics.localEvSpreadStops, 0.0f, 8.0f);
    evidence.localEvConflict = SaturateFloat(metrics.localEvConflict);
    evidence.localContrastPeak = SaturateFloat(metrics.localContrastPeak);
    evidence.centerShadowFraction = SaturateFloat(metrics.centerShadowFraction);
    evidence.centerHighlightFraction = SaturateFloat(metrics.centerHighlightFraction);
    evidence.clippedFraction = SaturateFloat(metrics.clippedFraction);
    evidence.highlightFraction = SaturateFloat(metrics.highlightFraction);
    evidence.shadowFraction = SaturateFloat(metrics.shadowFraction);
    evidence.shadowTextureRisk = SaturateFloat(metrics.shadowTextureRisk);
    evidence.haloRiskFraction = SaturateFloat(metrics.haloRiskFraction);
    evidence.lowSaturationFraction = SaturateFloat(metrics.lowSaturationFraction);
    evidence.highlightBandFraction = SaturateFloat(metrics.highlightBandFraction);
    evidence.highlightMeanLuma = SaturateFloat(metrics.highlightMeanLuma);
    evidence.highlightLowSaturationFraction = SaturateFloat(metrics.highlightLowSaturationFraction);
    evidence.highlightGrayRisk = SaturateFloat(metrics.highlightGrayRisk);
    evidence.highlightTileCoverage = SaturateFloat(metrics.highlightTileCoverage);
    evidence.highlightStructureScore = SaturateFloat(metrics.highlightStructureScore);
    evidence.meaningfulHighlightPressure = SaturateFloat(metrics.meaningfulHighlightPressure);
    evidence.contrastSpan = SaturateFloat(metrics.contrastSpan);
    evidence.peakTile = metrics.localDamageRiskPeakTile;
    evidence.localExposureHighlightCrowding = SaturateFloat(metrics.localExposureHighlightCrowding);
    evidence.localExposureShadowCrowding = SaturateFloat(metrics.localExposureShadowCrowding);
    evidence.localExposureHaloStress = SaturateFloat(metrics.localExposureHaloStress);
    evidence.localExposureFlatnessRisk = SaturateFloat(metrics.localExposureFlatnessRisk);
    evidence.localExposureDamageRisk = SaturateFloat(metrics.localExposureDamageRisk);
    evidence.subjectCenterPrior = SaturateFloat(metrics.subjectCenterPrior);
    evidence.subjectReadabilityPressure = SaturateFloat(metrics.subjectReadabilityPressure);
    evidence.subjectProtectionPressure = SaturateFloat(metrics.subjectProtectionPressure);
    evidence.subjectMoodPreservationPressure = SaturateFloat(metrics.subjectMoodPreservationPressure);
    evidence.subjectImportanceConfidence = SaturateFloat(metrics.subjectImportanceConfidence);

    const float broadHighlightArea = SaturateFloat(
        DevelopRegionRiskAbove(metrics.highlightFraction, 0.12f, 0.42f) * 0.42f +
        DevelopRegionRiskAbove(metrics.localHighlightPressure, 0.34f, 0.72f) * 0.42f +
        DevelopRegionRiskAbove(metrics.centerHighlightFraction, 0.18f, 0.46f) * 0.16f);
    evidence.broadHighlightPressure = SaturateFloat(
        broadHighlightArea +
        evidence.meaningfulHighlightPressure * 0.16f +
        DevelopRegionRiskAbove(metrics.clippedFraction, 0.006f, 0.026f) * 0.20f);
    evidence.localHighlightHotspotRisk = SaturateFloat(
        DevelopRegionRiskAbove(metrics.localHighlightPressure, 0.48f, 0.86f) * 0.40f +
        DevelopRegionRiskAbove(metrics.centerHighlightFraction, 0.26f, 0.58f) * 0.18f +
        DevelopRegionRiskAbove(metrics.clippedFraction, 0.004f, 0.020f) * 0.20f +
        evidence.meaningfulHighlightPressure * 0.10f +
        evidence.localExposureHighlightCrowding * 0.12f +
        metrics.localDamageRiskPeak * 0.16f +
        DevelopRegionRiskAbove(metrics.haloRiskFraction, 0.04f, 0.16f) * 0.06f);
    evidence.localShadowHotspotRisk = SaturateFloat(
        DevelopRegionRiskAbove(metrics.localShadowPressure, 0.58f, 0.90f) * 0.42f +
        DevelopRegionRiskAbove(metrics.centerShadowFraction, 0.38f, 0.72f) * 0.18f +
        DevelopRegionRiskAbove(metrics.shadowFraction, 0.48f, 0.82f) * 0.18f +
        evidence.localExposureShadowCrowding * 0.10f +
        metrics.localDamageRiskPeak * 0.12f +
        metrics.shadowTextureRisk * 0.10f);
    evidence.shadowNoiseLiftRisk = SaturateFloat(
        evidence.localShadowHotspotRisk * 0.42f +
        metrics.shadowTextureRisk * 0.42f +
        DevelopRegionRiskAbove(metrics.shadowFraction, 0.48f, 0.86f) * 0.16f);
    evidence.localHaloRisk = SaturateFloat(
        DevelopRegionRiskAbove(metrics.haloRiskFraction, 0.04f, 0.18f) * 0.48f +
        DevelopRegionRiskAbove(metrics.edgeContrast, 0.34f, 0.72f) * 0.22f +
        DevelopRegionRiskAbove(metrics.localContrastPeak, 0.76f, 0.96f) * 0.20f +
        evidence.localExposureHaloStress * 0.14f +
        evidence.localEvConflict * 0.08f +
        metrics.localDamageRiskMean * 0.10f);
    evidence.flatGrayRisk = SaturateFloat(
        DevelopRegionRiskBelow(metrics.contrastSpan, 0.30f, 0.12f) * 0.32f +
        DevelopRegionRiskBelow(metrics.localContrastPeak, 0.34f, 0.14f) * 0.22f +
        DevelopRegionRiskAbove(metrics.lowSaturationFraction, 0.70f, 0.94f) * 0.22f +
        evidence.highlightGrayRisk * 0.12f +
        evidence.localExposureFlatnessRisk * 0.14f +
        DevelopRegionRiskBelow(metrics.localLumaSpread, 0.16f, 0.04f) * 0.18f -
        DevelopRegionRiskAbove(metrics.highlightFraction, 0.22f, 0.42f) * 0.10f);
    evidence.localRangeConflict = SaturateFloat(
        evidence.localHighlightHotspotRisk * 0.34f +
        evidence.localShadowHotspotRisk * 0.28f +
        evidence.localHaloRisk * 0.18f +
        evidence.localEvConflict * 0.22f +
        evidence.localExposureDamageRisk * 0.12f +
        DevelopRegionRiskAbove(metrics.localLumaSpread, 0.28f, 0.58f) * 0.14f +
        metrics.localDamageRiskMean * 0.12f);
    evidence.brightnessHierarchyRisk = SaturateFloat(
        evidence.flatGrayRisk * 0.38f +
        evidence.highlightGrayRisk * 0.30f +
        DevelopRegionRiskBelow(metrics.localLumaSpread, 0.18f, 0.06f) * 0.20f +
        DevelopRegionRiskBelow(metrics.contrastSpan, 0.32f, 0.16f) * 0.18f +
        DevelopRegionRiskAbove(metrics.lowSaturationFraction, 0.72f, 0.94f) * 0.14f +
        DevelopRegionRiskAbove(metrics.highlightFraction, 0.62f, 0.86f) * 0.10f);
    evidence.smallSpecularLikely =
        metrics.clippedFraction > 0.0f &&
        metrics.clippedFraction < 0.010f &&
        metrics.highlightFraction < 0.18f &&
        metrics.localHighlightPressure < 0.38f &&
        evidence.meaningfulHighlightPressure < 0.28f;
    return evidence;
}

DevelopDynamicRangeRegionEvidence ResolveDevelopDynamicRangeRegionEvidence(
    const nlohmann::json& previousToneJson) {
    DevelopDynamicRangeRegionEvidence evidence;
    if (!previousToneJson.is_object()) {
        return evidence;
    }

    const std::string metricsStatus =
        previousToneJson.value("autoCandidateRenderMetricsStatus", std::string());
    if (metricsStatus != "ready" && metricsStatus != "partial") {
        evidence.source = metricsStatus.empty()
            ? "awaitingRenderedMetrics"
            : std::string("renderMetrics") + metricsStatus;
        return evidence;
    }

    const nlohmann::json renderedSolves =
        previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array());
    if (!renderedSolves.is_array()) {
        evidence.source = "renderMetricsMissing";
        return evidence;
    }

    EditorRenderWorker::DevelopCandidateRenderMetrics metrics;
    float renderScore = -1.0f;
    const std::array<std::pair<std::string, std::string>, 4> candidateSources = {{
        { previousToneJson.value("autoCandidateSelectedId", std::string()), "selectedRenderedCandidate" },
        { previousToneJson.value("autoCandidateRenderedFeedbackPreviousSelectedId", std::string()), "previousSelectedRenderedCandidate" },
        { std::string("base"), "baseRenderedCandidate" },
        { previousToneJson.value("autoCandidateRenderedBestId", std::string()), "bestRenderedCandidate" }
    }};

    for (const auto& [candidateId, source] : candidateSources) {
        if (candidateId.empty()) {
            continue;
        }
        if (TryReadRenderedCandidateMetrics(renderedSolves, candidateId, metrics, renderScore)) {
            return BuildDevelopDynamicRangeRegionEvidenceFromMetrics(
                metrics,
                source,
                candidateId,
                renderScore);
        }
    }

    evidence.source = "renderedCandidateMetricsUnavailable";
    return evidence;
}

bool TryReadLastRenderedHistoryMetrics(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore,
    std::string& outBestId) {
    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        if (it->value("fingerprint", static_cast<std::uint64_t>(0)) == excludedFingerprint) {
            continue;
        }
        if (!ReadDevelopRenderedMetricsFromJson(it->value("bestMetrics", nlohmann::json::object()), outMetrics)) {
            continue;
        }
        outScore = it->value("bestRenderScore", -1.0f);
        outBestId = it->value("bestId", std::string());
        return outScore >= 0.0f && !outBestId.empty();
    }
    return false;
}

struct DevelopRenderedFeedbackTrend {
    int historyCount = 0;
    int sameBestCount = 0;
    float scoreSpread = -1.0f;
    float nearestMetricDistance = -1.0f;
    std::string referenceId;
    std::string stopReason;
};

DevelopRenderedFeedbackTrend EvaluateDevelopRenderedFeedbackTrend(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& currentBestId,
    float currentBestScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& currentBestMetrics,
    float selectedRenderedScore,
    bool selectedRendered,
    int previousPass,
    bool refineFeedback) {
    DevelopRenderedFeedbackTrend trend;
    if (previousPass < 2 || currentBestId.empty() || currentBestScore < 0.0f) {
        return trend;
    }

    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        return trend;
    }

    float minScore = currentBestScore;
    float maxScore = currentBestScore;
    for (auto it = history.rbegin(); it != history.rend() && trend.historyCount < 3; ++it) {
        if (!it->is_object() ||
            it->value("fingerprint", static_cast<std::uint64_t>(0)) == excludedFingerprint ||
            it->value("successCount", 0) <= 0) {
            continue;
        }

        const std::string historyBestId = it->value("bestId", std::string());
        const float historyBestScore = it->value("bestRenderScore", -1.0f);
        if (historyBestId.empty() || historyBestScore < 0.0f) {
            continue;
        }

        EditorRenderWorker::DevelopCandidateRenderMetrics historyMetrics;
        if (!ReadDevelopRenderedMetricsFromJson(it->value("bestMetrics", nlohmann::json::object()), historyMetrics)) {
            continue;
        }

        if (trend.referenceId.empty()) {
            trend.referenceId = historyBestId;
        }
        ++trend.historyCount;
        if (historyBestId == currentBestId) {
            ++trend.sameBestCount;
        }
        const float metricDistance =
            EditorRenderWorker::CompareDevelopCandidateRenderMetrics(currentBestMetrics, historyMetrics);
        trend.nearestMetricDistance = trend.nearestMetricDistance < 0.0f
            ? metricDistance
            : std::min(trend.nearestMetricDistance, metricDistance);
        minScore = std::min(minScore, historyBestScore);
        maxScore = std::max(maxScore, historyBestScore);
    }

    if (trend.historyCount <= 0) {
        return trend;
    }

    trend.scoreSpread = maxScore - minScore;
    const bool repeatedSameBest =
        trend.historyCount >= 2 &&
        trend.sameBestCount >= 2;
    const bool stableRenderedShape =
        trend.nearestMetricDistance >= 0.0f &&
        trend.nearestMetricDistance < 0.055f;
    const bool flatScoreTrend =
        trend.scoreSpread >= 0.0f &&
        trend.scoreSpread < 0.030f;
    const bool limitedCurrentGain =
        !selectedRendered ||
        currentBestScore < selectedRenderedScore + 0.060f;

    if (repeatedSameBest && stableRenderedShape && flatScoreTrend) {
        trend.stopReason = refineFeedback
            ? "renderedRefineNoImprovementTrend"
            : "renderedFeedbackNoImprovementTrend";
    } else if (trend.historyCount >= 2 && stableRenderedShape && flatScoreTrend && limitedCurrentGain) {
        trend.stopReason = "renderedFeedbackStableTrend";
    }

    return trend;
}

struct DevelopRenderedMonotonicGuardDecision {
    bool stop = false;
    std::string reason;
    std::string metric;
    float previousValue = -1.0f;
    float currentValue = -1.0f;
    std::string referenceId;
};

bool TryReadLastSameIntentRefineMetrics(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& refineIntent,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore,
    std::string& outSelectedId) {
    if (refineIntent.empty()) {
        return false;
    }

    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object() ||
            it->value("fingerprint", static_cast<std::uint64_t>(0)) == excludedFingerprint ||
            it->value("refineIntent", std::string()) != refineIntent) {
            continue;
        }
        if (!ReadDevelopRenderedMetricsFromJson(it->value("selectedMetrics", nlohmann::json::object()), outMetrics)) {
            continue;
        }
        outScore = it->value("selectedRenderScore", -1.0f);
        outSelectedId = it->value("selectedId", std::string());
        return outScore >= 0.0f;
    }
    return false;
}

DevelopRenderedMonotonicGuardDecision EvaluateRenderedRefineMonotonicGuard(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& refineIntent,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& currentSelectedMetrics,
    float currentSelectedScore,
    bool currentSelectedScoreValid,
    int previousPass) {
    DevelopRenderedMonotonicGuardDecision decision;
    if (refineIntent.empty() || previousPass <= 0) {
        return decision;
    }

    EditorRenderWorker::DevelopCandidateRenderMetrics previousSelectedMetrics;
    float previousSelectedScore = -1.0f;
    std::string previousSelectedId;
    if (!TryReadLastSameIntentRefineMetrics(
            toneJson,
            excludedFingerprint,
            refineIntent,
            previousSelectedMetrics,
            previousSelectedScore,
            previousSelectedId)) {
        return decision;
    }

    const bool scoreClearlyImproved =
        currentSelectedScoreValid &&
        previousSelectedScore >= 0.0f &&
        currentSelectedScore >= previousSelectedScore + 0.040f;
    auto stopIfWorse = [&](const char* reason,
                           const char* metric,
                           float previousValue,
                           float currentValue,
                           float allowedIncrease,
                           float floor,
                           float severeFloor) {
        if (decision.stop) {
            return;
        }
        const bool materiallyWorse =
            currentValue > previousValue + allowedIncrease &&
            currentValue > floor;
        if (!materiallyWorse) {
            return;
        }
        // Same-intent refinements get one chance to improve. If the protected
        // risk worsens without a clear score gain, stop instead of chasing the
        // same direction again.
        if (!scoreClearlyImproved || currentValue > severeFloor) {
            decision.stop = true;
            decision.reason = reason;
            decision.metric = metric;
            decision.previousValue = previousValue;
            decision.currentValue = currentValue;
            decision.referenceId = previousSelectedId;
        }
    };

    if (refineIntent == "openShadows" || refineIntent == "cleanShadows") {
        stopIfWorse(
            "renderedRefineMonotonicShadowRisk",
            "shadowTextureRisk",
            previousSelectedMetrics.shadowTextureRisk,
            currentSelectedMetrics.shadowTextureRisk,
            0.055f,
            0.52f,
            0.72f);
        stopIfWorse(
            "renderedRefineMonotonicShadowRisk",
            "localShadowPressure",
            previousSelectedMetrics.localShadowPressure,
            currentSelectedMetrics.localShadowPressure,
            0.080f,
            0.50f,
            0.76f);
        stopIfWorse(
            "renderedRefineMonotonicShadowRisk",
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    } else if (refineIntent == "protectHighlights" || refineIntent == "brightenMids") {
        const char* reason = refineIntent == "brightenMids"
            ? "renderedRefineMonotonicBrightnessRisk"
            : "renderedRefineMonotonicHighlightRisk";
        stopIfWorse(
            reason,
            "clippedFraction",
            previousSelectedMetrics.clippedFraction,
            currentSelectedMetrics.clippedFraction,
            0.004f,
            0.008f,
            0.018f);
        stopIfWorse(
            reason,
            "localHighlightPressure",
            previousSelectedMetrics.localHighlightPressure,
            currentSelectedMetrics.localHighlightPressure,
            0.080f,
            0.50f,
            0.70f);
        stopIfWorse(
            reason,
            "haloRiskFraction",
            previousSelectedMetrics.haloRiskFraction,
            currentSelectedMetrics.haloRiskFraction,
            0.040f,
            0.08f,
            0.16f);
        stopIfWorse(
            reason,
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    } else if (refineIntent == "addContrast") {
        stopIfWorse(
            "renderedRefineMonotonicContrastRisk",
            "haloRiskFraction",
            previousSelectedMetrics.haloRiskFraction,
            currentSelectedMetrics.haloRiskFraction,
            0.040f,
            0.08f,
            0.16f);
        stopIfWorse(
            "renderedRefineMonotonicContrastRisk",
            "localContrastPeak",
            previousSelectedMetrics.localContrastPeak,
            currentSelectedMetrics.localContrastPeak,
            0.100f,
            0.76f,
            0.90f);
        stopIfWorse(
            "renderedRefineMonotonicContrastRisk",
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    } else if (refineIntent == "preserveTexture") {
        const float edgeGain =
            currentSelectedMetrics.edgeContrast - previousSelectedMetrics.edgeContrast;
        if (edgeGain < 0.025f) {
            stopIfWorse(
                "renderedRefineMonotonicTextureRisk",
                "shadowTextureRisk",
                previousSelectedMetrics.shadowTextureRisk,
                currentSelectedMetrics.shadowTextureRisk,
                0.060f,
                0.48f,
                0.70f);
        }
        stopIfWorse(
            "renderedRefineMonotonicTextureRisk",
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    }

    return decision;
}

bool HasDevelopAutoCandidateId(
    const std::vector<DevelopAutoCandidateSolve>& candidates,
    const std::string& id) {
    if (id.empty()) {
        return true;
    }
    return std::any_of(candidates.begin(), candidates.end(), [&](const DevelopAutoCandidateSolve& candidate) {
        return candidate.id == id;
    });
}

bool ReadDevelopCandidateGuidanceFromJson(
    const nlohmann::json& value,
    EditorNodeGraph::DevelopAutoGuidance& guidance) {
    if (!value.is_object()) {
        return false;
    }

    guidance.autoStrength = value.value("autoStrength", guidance.autoStrength);
    guidance.exposureBias = value.value("brightnessIntent", guidance.exposureBias);
    guidance.dynamicRange = value.value("dynamicRange", guidance.dynamicRange);
    guidance.shadowLift = value.value("shadowLift", guidance.shadowLift);
    guidance.highlightGuard = value.value("highlightGuard", guidance.highlightGuard);
    guidance.highlightCharacter = value.value("highlightCharacter", guidance.highlightCharacter);
    guidance.contrastBias = value.value("contrastBias", guidance.contrastBias);
    guidance.subjectSceneBias = value.value("subjectSceneBias", guidance.subjectSceneBias);
    guidance.moodReadabilityBias = value.value("moodReadabilityBias", guidance.moodReadabilityBias);
    EditorModule::NormalizeDevelopAutoGuidance(guidance);
    return true;
}

bool TryReadDevelopAutoCandidateFromToneJson(
    const nlohmann::json& toneJson,
    const std::string& candidateId,
    DevelopAutoCandidateSolve& outCandidate) {
    if (!toneJson.is_object() || candidateId.empty()) {
        return false;
    }

    const nlohmann::json candidates =
        toneJson.value("autoCandidateSolves", nlohmann::json::array());
    if (!candidates.is_array()) {
        return false;
    }

    for (const nlohmann::json& candidateJson : candidates) {
        if (!candidateJson.is_object() ||
            candidateJson.value("id", std::string()) != candidateId) {
            continue;
        }

        EditorNodeGraph::DevelopAutoGuidance guidance;
        if (!ReadDevelopCandidateGuidanceFromJson(
                candidateJson.value("guidance", nlohmann::json::object()),
                guidance)) {
            return false;
        }

        outCandidate.id = candidateId;
        outCandidate.label = candidateJson.value("label", candidateId);
        outCandidate.reason =
            "Carried forward the previous authored candidate so rendered feedback can converge across passes.";
        outCandidate.guidance = guidance;
        outCandidate.guidanceFingerprint =
            candidateJson.value(
                "guidanceFingerprint",
                BuildDevelopAutoCandidateGuidanceFingerprint(candidateId, guidance));
        outCandidate.score = std::clamp(candidateJson.value("score", 0.50f), 0.0f, 1.0f);
        outCandidate.scoreComponents =
            candidateJson.value("scoreComponents", nlohmann::json::object());
        if (!outCandidate.scoreComponents.is_object()) {
            outCandidate.scoreComponents = nlohmann::json::object();
        }
        outCandidate.rejected = false;
        outCandidate.duplicate = false;
        outCandidate.rememberedRejection = false;
        outCandidate.renderedMemoryRejected = false;
        Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
        outCandidate.whiteBalanceProbe =
            TryResolveDevelopWhiteBalanceProbeMode(candidateId, whiteBalanceMode);
        outCandidate.whiteBalanceMode = whiteBalanceMode;
        outCandidate.rejectReason.clear();
        return true;
    }
    return false;
}

std::vector<std::string> CollectDevelopRenderedSurvivorCandidateIdsForCarryForward(
    const nlohmann::json& toneJson,
    std::size_t maxIds) {
    std::vector<std::pair<float, std::string>> scoredIds;
    if (maxIds == 0) {
        return {};
    }

    const nlohmann::json renderedSolves =
        toneJson.value("autoCandidateRenderedSolves", nlohmann::json::array());
    if (!renderedSolves.is_array()) {
        return {};
    }

    for (const nlohmann::json& rendered : renderedSolves) {
        if (!rendered.is_object() || !rendered.value("success", false)) {
            continue;
        }
        const std::string id = rendered.value("id", std::string());
        if (id.empty()) {
            continue;
        }
        const std::string status = rendered.value("renderedStatus", std::string());
        if (status == "renderedRejectedDamage" || status == "renderedDuplicate") {
            continue;
        }
        const float renderScore = rendered.value("renderScore", -1.0f);
        if (renderScore < 0.0f) {
            continue;
        }
        scoredIds.emplace_back(renderScore, id);
    }

    std::sort(scoredIds.begin(), scoredIds.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    std::vector<std::string> ids;
    for (const auto& scored : scoredIds) {
        if (std::find(ids.begin(), ids.end(), scored.second) != ids.end()) {
            continue;
        }
        ids.push_back(scored.second);
        if (ids.size() >= maxIds) {
            break;
        }
    }
    return ids;
}

bool WouldRenderedFeedbackReverseRecentAdoption(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId) {
    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array() || selectedCandidateId.empty() || bestCandidateId.empty()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        const std::string action = it->value("action", std::string());
        if (action != "adopted" && action != "merged" && action != "refined" && action != "solveRequested") {
            continue;
        }
        const std::string previousSelected = it->value("selectedId", std::string());
        const std::string previousBest = it->value("bestId", std::string());
        return previousSelected == bestCandidateId && previousBest == selectedCandidateId;
    }
    return false;
}

bool IsRenderedFeedbackMergeCandidateId(const std::string& candidateId) {
    return
        candidateId == "renderedFeedbackMerge" ||
        candidateId == "renderedFeedbackPairMerge" ||
        candidateId == "renderedFeedbackEnsembleMerge";
}

std::string RepeatedRenderedChoiceStopReason(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId,
    float selectedRenderedScore,
    bool selectedRendered,
    float bestRenderedScore) {
    if (!selectedRendered || selectedCandidateId.empty() || bestCandidateId.empty()) {
        return {};
    }

    const std::string previousAction =
        toneJson.value("autoCandidateRenderedFeedbackAction", std::string());
    const std::string previousBestId =
        toneJson.value("autoCandidateRenderedFeedbackBestId", std::string());
    if (previousBestId.empty() || previousBestId != bestCandidateId) {
        return {};
    }

    if (previousAction == "merged" && IsRenderedFeedbackMergeCandidateId(selectedCandidateId)) {
        const float currentGap = bestRenderedScore - selectedRenderedScore;
        if (currentGap <= 0.060f) {
            return "renderedMergeConverged";
        }

        const float previousBestScore =
            toneJson.value("autoCandidateRenderedFeedbackBestScore", -1.0f);
        const float previousSelectedScore =
            toneJson.value("autoCandidateRenderedFeedbackPreviousSelectedScore", -1.0f);
        if (previousBestScore >= 0.0f && previousSelectedScore >= 0.0f) {
            const float previousGap = previousBestScore - previousSelectedScore;
            if (currentGap >= previousGap - 0.010f) {
                return "renderedMergeDidNotImprove";
            }
        }
    }

    if (previousAction == "adopted" &&
        selectedCandidateId != bestCandidateId &&
        bestRenderedScore < selectedRenderedScore + 0.040f) {
        return "renderedAdoptionNoFurtherGain";
    }

    return {};
}

bool WouldRepeatUnhelpfulRenderedRefinement(
    const nlohmann::json& toneJson,
    const std::string& refineIntent,
    float selectedRenderedScore,
    bool selectedRendered) {
    if (refineIntent.empty()) {
        return false;
    }
    if (toneJson.value("autoCandidateRenderedFeedbackAction", std::string()) != "refined" ||
        toneJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) != refineIntent) {
        return false;
    }

    if (!selectedRendered) {
        return toneJson.value("autoCandidateRenderedFeedbackPass", 0) > 0;
    }

    const float previousSelectedScore =
        toneJson.value("autoCandidateRenderedFeedbackPreviousSelectedScore", -1.0f);
    if (previousSelectedScore >= 0.0f && selectedRenderedScore < previousSelectedScore + 0.025f) {
        return true;
    }

    // Give a repeated same-direction refine one chance to prove improvement, then stop.
    return toneJson.value("autoCandidateRenderedFeedbackPass", 0) >= 2;
}

bool TryApplyRenderedEnsembleMergeToSolve(
    DevelopAutoCandidateSolveResult& result,
    const nlohmann::json& previousToneJson,
    const DevelopAutoCandidateSolve& selectedCandidate) {
    if (!previousToneJson.value("autoCandidateRenderedEnsembleMergeSuggested", false)) {
        return false;
    }

    const nlohmann::json ensembleIdsJson =
        previousToneJson.value("autoCandidateRenderedEnsembleMergeIds", nlohmann::json::array());
    const nlohmann::json ensembleScoresJson =
        previousToneJson.value("autoCandidateRenderedEnsembleMergeScores", nlohmann::json::array());
    std::vector<std::string> ensembleIds;
    if (ensembleIdsJson.is_array()) {
        for (const nlohmann::json& idJson : ensembleIdsJson) {
            const std::string candidateId = idJson.is_string() ? idJson.get<std::string>() : std::string();
            if (!candidateId.empty() &&
                std::find(ensembleIds.begin(), ensembleIds.end(), candidateId) == ensembleIds.end()) {
                ensembleIds.push_back(candidateId);
                if (ensembleIds.size() >= 3) {
                    break;
                }
            }
        }
    }
    if (ensembleIds.size() < 3) {
        return false;
    }

    auto findCandidateById = [&](const std::string& candidateId) {
        return std::find_if(
            result.candidates.begin(),
            result.candidates.end(),
            [&](const DevelopAutoCandidateSolve& candidate) {
                return candidate.id == candidateId && !candidate.rejected;
            });
    };
    auto renderedScoreForId = [&](const std::string& candidateId, std::size_t scoreIndex) {
        if (ensembleScoresJson.is_array() &&
            scoreIndex < ensembleScoresJson.size() &&
            ensembleScoresJson[scoreIndex].is_number()) {
            return ensembleScoresJson[scoreIndex].get<float>();
        }
        float renderedScore = -1.0f;
        if (TryReadRenderedCandidateScore(
                previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
                candidateId,
                renderedScore)) {
            return renderedScore;
        }
        return -1.0f;
    };

    auto firstIt = findCandidateById(ensembleIds[0]);
    auto secondIt = findCandidateById(ensembleIds[1]);
    auto thirdIt = findCandidateById(ensembleIds[2]);
    if (firstIt == result.candidates.end() ||
        secondIt == result.candidates.end() ||
        thirdIt == result.candidates.end()) {
        return false;
    }

    const float firstRenderedScore = renderedScoreForId(ensembleIds[0], 0);
    const float secondRenderedScore = renderedScoreForId(ensembleIds[1], 1);
    const float thirdRenderedScore = renderedScoreForId(ensembleIds[2], 2);
    const float maxRenderedScore =
        std::max(firstRenderedScore, std::max(secondRenderedScore, thirdRenderedScore));
    const float minRenderedScore =
        std::min(firstRenderedScore, std::min(secondRenderedScore, thirdRenderedScore));
    const float scoreSpread =
        previousToneJson.value(
            "autoCandidateRenderedEnsembleMergeScoreSpread",
            maxRenderedScore - minRenderedScore);
    const float metricSpread =
        previousToneJson.value("autoCandidateRenderedEnsembleMergeMetricSpread", 0.0f);
    const float firstSecondDistance =
        DevelopAutoCandidateDistance(firstIt->guidance, secondIt->guidance);
    const float firstThirdDistance =
        DevelopAutoCandidateDistance(firstIt->guidance, thirdIt->guidance);
    const float secondThirdDistance =
        DevelopAutoCandidateDistance(secondIt->guidance, thirdIt->guidance);
    const float averageGuidanceDistance =
        (firstSecondDistance + firstThirdDistance + secondThirdDistance) / 3.0f;
    const int distinctPairCount =
        (firstSecondDistance >= 0.13f ? 1 : 0) +
        (firstThirdDistance >= 0.13f ? 1 : 0) +
        (secondThirdDistance >= 0.13f ? 1 : 0);

    const bool ensembleScoresAreStrong =
        firstRenderedScore >= 0.60f &&
        secondRenderedScore >= 0.56f &&
        thirdRenderedScore >= 0.52f &&
        scoreSpread <= 0.22f;
    const bool ensembleIsMeaningfullyDifferent =
        metricSpread >= 0.10f ||
        averageGuidanceDistance >= 0.16f ||
        distinctPairCount >= 2;
    if (!ensembleScoresAreStrong || !ensembleIsMeaningfullyDifferent) {
        return false;
    }

    const DevelopAutoCandidateSolve firstCandidate = *firstIt;
    const DevelopAutoCandidateSolve secondCandidate = *secondIt;
    const DevelopAutoCandidateSolve thirdCandidate = *thirdIt;
    if (firstCandidate.whiteBalanceProbe ||
        secondCandidate.whiteBalanceProbe ||
        thirdCandidate.whiteBalanceProbe) {
        return false;
    }
    float firstWeight = std::max(0.01f, firstRenderedScore);
    float secondWeight = std::max(0.01f, secondRenderedScore);
    float thirdWeight = std::max(0.01f, thirdRenderedScore);
    const float initialWeightSum = firstWeight + secondWeight + thirdWeight;
    firstWeight = std::clamp(firstWeight / initialWeightSum, 0.22f, 0.46f);
    secondWeight = std::clamp(secondWeight / initialWeightSum, 0.22f, 0.46f);
    thirdWeight = std::clamp(thirdWeight / initialWeightSum, 0.22f, 0.46f);
    const float clampedWeightSum = firstWeight + secondWeight + thirdWeight;
    firstWeight /= clampedWeightSum;
    secondWeight /= clampedWeightSum;
    thirdWeight /= clampedWeightSum;

    DevelopAutoCandidateSolve merged;
    merged.id = "renderedFeedbackEnsembleMerge";
    merged.label = "Rendered Ensemble Merge";
    merged.reason =
        "Merged three strong, distinct rendered survivors in authored settings space so the next solve can reconcile a broader intent set without blending final pixels.";
    merged.guidance = BlendDevelopAutoCandidateGuidance(
        firstCandidate.guidance,
        secondCandidate.guidance,
        thirdCandidate.guidance,
        firstWeight,
        secondWeight,
        thirdWeight);
    merged.score = std::min(
        1.0f,
        std::max(firstCandidate.score, std::max(secondCandidate.score, thirdCandidate.score)) + 0.022f);
    result.candidates.push_back(merged);
    result.selectionSource = "renderedMetricsEnsembleMerge";
    result.authoredGuidance = merged.guidance;
    result.selectedId = merged.id;
    result.selectedLabel = merged.label;
    result.selectedScore = merged.score;
    ClearDevelopResultWhiteBalanceProbe(result);
    result.mergeApplied = true;
    result.mergeFirstId = firstCandidate.id;
    result.mergeSecondId = secondCandidate.id;
    result.mergeThirdId = thirdCandidate.id;
    result.mergeFirstWeight = firstWeight;
    result.mergeSecondWeight = secondWeight;
    result.mergeThirdWeight = thirdWeight;
    result.renderedFeedbackAction = "merged";
    result.renderedFeedbackRevisionStage =
        DevelopRenderedRevisionStageForGuidanceDelta(
            selectedCandidate.guidance,
            merged.guidance,
            merged.id);
    result.renderedFeedbackRevisionReason =
        DevelopRenderedRevisionStageReason(
            result.renderedFeedbackRevisionStage,
            "Rendered ensemble merge affects multiple authored stages; the next pass should validate the broader merged intent against actual rendered output.");
    return true;
}

bool ApplyRenderedCandidateFeedbackToSolve(
    DevelopAutoCandidateSolveResult& result,
    const nlohmann::json& previousToneJson,
    std::uint64_t preliminaryFingerprint) {
    constexpr float kMinimumRenderedScore = 0.48f;

    if (!previousToneJson.is_object() || preliminaryFingerprint == 0) {
        return false;
    }
    auto stopWithoutApplying = [&](std::string reason) {
        result.renderedFeedbackStopReason = std::move(reason);
        result.renderedFeedbackStopIsConverged =
            EditorModule::IsDevelopRenderedFeedbackStopConvergedReason(
                result.renderedFeedbackStopReason);
        return false;
    };

    const std::string metricsStatus =
        previousToneJson.value("autoCandidateRenderMetricsStatus", std::string());
    if (metricsStatus != "ready" && metricsStatus != "partial") {
        return stopWithoutApplying("renderedMetricsNotReady");
    }
    const std::uint64_t renderedFingerprint =
        previousToneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0));
    const std::string bestId =
        previousToneJson.value("autoCandidateRenderedBestId", std::string());
    const float bestScore =
        previousToneJson.value("autoCandidateRenderedBestScore", -1.0f);
    const std::string bestRelativeStatus =
        previousToneJson.value("autoCandidateRenderedBestRelativeStatus", std::string());
    const float bestRelativeRepairBonus =
        previousToneJson.value("autoCandidateRenderedBestRelativeRepairBonus", 0.0f);
    const float bestRelativeDistanceBonus =
        previousToneJson.value("autoCandidateRenderedBestRelativeDistanceBonus", 0.0f);
    const float bestRelativeRegressionPenalty =
        previousToneJson.value("autoCandidateRenderedBestRelativeRegressionPenalty", 0.0f);
    const std::string refineIntent =
        previousToneJson.value("autoCandidateRenderedRefineIntent", std::string());
    const std::string requestedRevisionStage =
        previousToneJson.value("autoCandidateRenderedRevisionStage", std::string());
    const std::string stageBoundarySignal =
        previousToneJson.value("autoCandidateRenderedStageBoundarySignal", std::string());
    auto resolveRenderedFeedbackStage = [&](const std::string& fallbackStage) {
        if (stageBoundarySignal == "finishToneOnly" && requestedRevisionStage == "finishTone") {
            return std::string("finishTone");
        }
        return fallbackStage;
    };
    auto resolveRenderedFeedbackReason = [&](const std::string& stage, const std::string& fallbackReason) {
        if (stageBoundarySignal == "finishToneOnly" && stage == "finishTone") {
            const std::string requestedReason =
                previousToneJson.value("autoCandidateRenderedRevisionReason", std::string());
            if (!requestedReason.empty()) {
                return requestedReason;
            }
        }
        return DevelopRenderedRevisionStageReason(stage, fallbackReason);
    };
    const bool hasRefineIntent =
        refineIntent == "brightenMids" ||
        refineIntent == "openShadows" ||
        refineIntent == "protectHighlights" ||
        refineIntent == "addContrast" ||
        refineIntent == "cleanShadows" ||
        refineIntent == "preserveTexture";
    const std::uint64_t previousSolveFingerprint =
        previousToneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
    const bool renderedMetricsMatchCurrentSolve = renderedFingerprint == preliminaryFingerprint;
    const bool renderedMetricsMatchRefineBase =
        hasRefineIntent &&
        previousSolveFingerprint != 0 &&
        renderedFingerprint == previousSolveFingerprint;
    const bool renderedMetricsMatchPreviousSolve =
        previousSolveFingerprint != 0 &&
        renderedFingerprint == previousSolveFingerprint;
    if (!renderedMetricsMatchCurrentSolve &&
        !renderedMetricsMatchRefineBase &&
        !renderedMetricsMatchPreviousSolve) {
        return stopWithoutApplying("renderedMetricsForPreviousSolve");
    }
    if (previousToneJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFingerprint) {
        return stopWithoutApplying("renderedFeedbackAlreadyApplied");
    }

    const int previousPass = previousToneJson.value("autoCandidateRenderedFeedbackPass", 0);
    const DevelopConvergenceAdmissionPolicy admissionPolicy =
        ResolveDevelopConvergenceAdmissionPolicy(previousToneJson, previousPass, hasRefineIntent);
    result.renderedFeedbackAdmissionBaseMinimumImprovement =
        admissionPolicy.baseMinimumImprovement;
    result.renderedFeedbackAdmissionMinimumImprovement =
        admissionPolicy.minimumImprovement;
    result.renderedFeedbackAdmissionTightened = admissionPolicy.tightened;
    result.renderedFeedbackAdmissionReason = admissionPolicy.reason;
    result.renderedFeedbackAdmissionEvidenceState = admissionPolicy.evidenceState;
    result.renderedFeedbackAdmissionEvidenceDecision = admissionPolicy.evidenceDecision;
    result.renderedFeedbackAdmissionEvidencePass = admissionPolicy.evidencePass;

    if (previousPass >= kDevelopRenderedFeedbackMaxPasses) {
        return stopWithoutApplying("renderedFeedbackPassLimit");
    }

    if ((bestId.empty() || bestScore < kMinimumRenderedScore) && !hasRefineIntent) {
        return stopWithoutApplying(bestId.empty() ? "noRenderedBestCandidate" : "renderedBestBelowQualityFloor");
    }

    float selectedRenderedScore = -1.0f;
    const bool selectedRendered =
        TryReadRenderedCandidateScore(
            previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
            result.selectedId,
            selectedRenderedScore);
    result.renderedFeedbackImprovement =
        selectedRendered ? (bestScore - selectedRenderedScore) : -1.0f;
    if (!hasRefineIntent && previousPass > 0 && !selectedRendered) {
        const std::string previousAction =
            previousToneJson.value("autoCandidateRenderedFeedbackAction", std::string());
        const std::string previousBestId =
            previousToneJson.value("autoCandidateRenderedFeedbackBestId", std::string());
        if (previousAction == "adopted" && previousBestId == bestId) {
            return stopWithoutApplying("renderedAdoptionNoFurtherGain");
        }
    }

    EditorRenderWorker::DevelopCandidateRenderMetrics currentSelectedMetrics;
    float currentSelectedMetricScore = -1.0f;
    const bool currentSelectedMetricsValid =
        TryReadRenderedCandidateMetrics(
            previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
            result.selectedId,
            currentSelectedMetrics,
            currentSelectedMetricScore);
    EditorRenderWorker::DevelopCandidateRenderMetrics currentBestMetrics;
    float currentBestMetricScore = -1.0f;
    const bool currentBestMetricsValid =
        TryReadRenderedCandidateMetrics(
            previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
            bestId,
            currentBestMetrics,
            currentBestMetricScore);
    EditorRenderWorker::DevelopCandidateRenderMetrics previousBestMetrics;
    float previousBestMetricScore = -1.0f;
    std::string previousBestMetricId;
    const bool previousBestMetricsValid =
        TryReadLastRenderedHistoryMetrics(
            previousToneJson,
            renderedFingerprint,
            previousBestMetrics,
            previousBestMetricScore,
            previousBestMetricId);
    if (currentBestMetricsValid && previousBestMetricsValid && previousPass > 0) {
        result.renderedFeedbackStabilityDistance =
            EditorRenderWorker::CompareDevelopCandidateRenderMetrics(currentBestMetrics, previousBestMetrics);
        result.renderedFeedbackStabilityScoreDelta =
            std::fabs(bestScore - previousBestMetricScore);
        result.renderedFeedbackStabilityReferenceId = previousBestMetricId;
        const bool renderedStateStable =
            result.renderedFeedbackStabilityDistance < 0.045f &&
            result.renderedFeedbackStabilityScoreDelta < 0.020f;
        const bool noUsefulRenderedImprovement =
            !selectedRendered ||
            bestId == result.selectedId ||
            bestScore < selectedRenderedScore + admissionPolicy.minimumImprovement;
        if (renderedStateStable && noUsefulRenderedImprovement) {
            return stopWithoutApplying("renderedMetricsStable");
        }
    }
    if (currentBestMetricsValid) {
        const DevelopRenderedFeedbackTrend trend =
            EvaluateDevelopRenderedFeedbackTrend(
                previousToneJson,
                renderedFingerprint,
                bestId,
                bestScore,
                currentBestMetrics,
                selectedRenderedScore,
                selectedRendered,
                previousPass,
                hasRefineIntent);
        result.renderedFeedbackTrendHistoryCount = trend.historyCount;
        result.renderedFeedbackTrendSameBestCount = trend.sameBestCount;
        result.renderedFeedbackTrendScoreSpread = trend.scoreSpread;
        result.renderedFeedbackTrendNearestDistance = trend.nearestMetricDistance;
        result.renderedFeedbackTrendReferenceId = trend.referenceId;
        if (!trend.stopReason.empty()) {
            return stopWithoutApplying(trend.stopReason);
        }
    }
    if (hasRefineIntent && currentSelectedMetricsValid) {
        const DevelopRenderedMonotonicGuardDecision monotonicGuard =
            EvaluateRenderedRefineMonotonicGuard(
                previousToneJson,
                renderedFingerprint,
                refineIntent,
                currentSelectedMetrics,
                currentSelectedMetricScore,
                currentSelectedMetricScore >= 0.0f,
                previousPass);
        result.renderedFeedbackMonotonicMetric = monotonicGuard.metric;
        result.renderedFeedbackMonotonicPreviousValue = monotonicGuard.previousValue;
        result.renderedFeedbackMonotonicCurrentValue = monotonicGuard.currentValue;
        result.renderedFeedbackMonotonicReferenceId = monotonicGuard.referenceId;
        if (monotonicGuard.stop) {
            return stopWithoutApplying(monotonicGuard.reason);
        }
    }

    if (!hasRefineIntent &&
        bestId == result.selectedId) {
        return stopWithoutApplying("selectedCandidateStillBest");
    }
    if (!hasRefineIntent &&
        WouldRenderedFeedbackReverseRecentAdoption(previousToneJson, result.selectedId, bestId)) {
        return stopWithoutApplying("wouldReverseRecentRenderedAdoption");
    }
    if (!hasRefineIntent &&
        selectedRendered &&
        bestScore < selectedRenderedScore + admissionPolicy.minimumImprovement) {
        const bool clearedBaseButNotEvidenceThreshold =
            admissionPolicy.tightened &&
            bestScore >= selectedRenderedScore + admissionPolicy.baseMinimumImprovement;
        return stopWithoutApplying(
            clearedBaseButNotEvidenceThreshold
                ? "convergenceAdmissionNoMeaningfulImprovement"
                : "noMeaningfulRenderedImprovement");
    }
    if (!hasRefineIntent &&
        selectedRendered &&
        (bestRelativeStatus == "regressedAgainstSelected" ||
         bestRelativeStatus == "missedActiveRepair") &&
        bestRelativeRegressionPenalty >
            bestRelativeRepairBonus + bestRelativeDistanceBonus + 0.012f &&
        bestScore < selectedRenderedScore + 0.055f) {
        return stopWithoutApplying("renderedBestRelativeRegression");
    }
    const std::string repeatedChoiceStopReason =
        !hasRefineIntent
            ? RepeatedRenderedChoiceStopReason(
                previousToneJson,
                result.selectedId,
                bestId,
                selectedRenderedScore,
                selectedRendered,
                bestScore)
            : std::string();
    if (!repeatedChoiceStopReason.empty()) {
        return stopWithoutApplying(repeatedChoiceStopReason);
    }
    if (hasRefineIntent &&
        WouldRepeatUnhelpfulRenderedRefinement(
            previousToneJson,
            refineIntent,
            selectedRenderedScore,
            selectedRendered)) {
        return stopWithoutApplying("renderedRefineDidNotImprove");
    }

    auto selectedIt = std::find_if(
        result.candidates.begin(),
        result.candidates.end(),
        [&result](const DevelopAutoCandidateSolve& candidate) {
            return candidate.id == result.selectedId && !candidate.rejected;
        });
    if (selectedIt == result.candidates.end()) {
        return stopWithoutApplying("selectedCandidateUnavailableForRenderedFeedback");
    }

    DevelopAutoCandidateSolve bestCandidate;
    if (!hasRefineIntent) {
        auto bestIt = std::find_if(
            result.candidates.begin(),
            result.candidates.end(),
            [&bestId](const DevelopAutoCandidateSolve& candidate) {
                return candidate.id == bestId && !candidate.rejected;
            });
        if (bestIt == result.candidates.end()) {
            return stopWithoutApplying("renderedBestCandidateUnavailableForSolve");
        }
        bestCandidate = *bestIt;
    }

    result.renderedFeedbackApplied = true;
    result.renderedFeedbackSourceFingerprint = renderedFingerprint;
    result.renderedFeedbackPass = previousPass + 1;
    result.renderedFeedbackPreviousSelectedId = result.selectedId;
    result.renderedFeedbackPreviousSelectedScore =
        selectedRendered ? selectedRenderedScore : result.selectedScore;
    result.renderedFeedbackBestId = bestId;
    result.renderedFeedbackBestScore = bestScore;

    const DevelopAutoCandidateSolve selectedCandidate = *selectedIt;
    if (hasRefineIntent) {
        const std::string preferredRenderedCandidateId =
            PreferredRenderedRefineCandidateId(refineIntent);
        if (!preferredRenderedCandidateId.empty()) {
            auto renderedCandidateIt = std::find_if(
                result.candidates.begin(),
                result.candidates.end(),
                [&](const DevelopAutoCandidateSolve& candidate) {
                    return candidate.id == preferredRenderedCandidateId && !candidate.rejected;
                });
            if (renderedCandidateIt != result.candidates.end()) {
                result.selectionSource = "renderedMetricsRefine";
                result.authoredGuidance = renderedCandidateIt->guidance;
                result.selectedId = renderedCandidateIt->id;
                result.selectedLabel = renderedCandidateIt->label;
                result.selectedScore = std::min(1.0f, std::max(renderedCandidateIt->score, selectedCandidate.score + 0.010f));
                SetDevelopResultWhiteBalanceProbe(result, *renderedCandidateIt);
                result.mergeApplied = false;
                result.mergeFirstId.clear();
                result.mergeSecondId.clear();
                result.mergeThirdId.clear();
                result.mergeFirstWeight = 1.0f;
                result.mergeSecondWeight = 0.0f;
                result.mergeThirdWeight = 0.0f;
                result.renderedFeedbackAction = "refined";
                result.renderedFeedbackRefineIntent = refineIntent;
                result.renderedFeedbackRefineReason = renderedCandidateIt->reason;
                result.renderedFeedbackRevisionStage =
                    resolveRenderedFeedbackStage(
                        DevelopRenderedRevisionStageForRefineIntent(refineIntent));
                result.renderedFeedbackRevisionReason =
                    resolveRenderedFeedbackReason(
                        result.renderedFeedbackRevisionStage,
                        renderedCandidateIt->reason);
                return true;
            }
        }

        // Refine the current rendered winner from measured output mismatch, with damped moves only.
        DevelopAutoCandidateSolve refined;
        refined.id = "renderedFeedbackRefine";
        refined.label = "Rendered Feedback Refine";
        refined.reason = previousToneJson.value(
            "autoCandidateRenderedRefineReason",
            std::string("Adjusted the current selected candidate from rendered metrics in authored settings space."));
        refined.guidance = selectedCandidate.guidance;
        refined.whiteBalanceProbe = selectedCandidate.whiteBalanceProbe;
        refined.whiteBalanceMode = selectedCandidate.whiteBalanceMode;
        if (refineIntent == "brightenMids") {
            refined.label = "Rendered Brightness Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                0.08f,
                0.06f,
                0.10f,
                0.04f,
                0.00f,
                -0.03f);
        } else if (refineIntent == "openShadows") {
            refined.label = "Rendered Shadow Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                0.04f,
                0.10f,
                0.16f,
                0.06f,
                -0.02f,
                -0.04f);
        } else if (refineIntent == "protectHighlights") {
            refined.label = "Rendered Highlight Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                -0.08f,
                0.16f,
                0.04f,
                0.24f,
                -0.04f,
                -0.08f);
        } else if (refineIntent == "addContrast") {
            refined.label = "Rendered Contrast Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                0.00f,
                -0.05f,
                -0.05f,
                0.02f,
                0.06f,
                0.16f);
        }
        refined.score = std::min(1.0f, selectedCandidate.score + 0.012f);
        result.candidates.push_back(refined);
        result.selectionSource = "renderedMetricsRefine";
        result.authoredGuidance = refined.guidance;
        result.selectedId = refined.id;
        result.selectedLabel = refined.label;
        result.selectedScore = refined.score;
        SetDevelopResultWhiteBalanceProbe(result, refined);
        result.mergeApplied = false;
        result.mergeFirstId.clear();
        result.mergeSecondId.clear();
        result.mergeThirdId.clear();
        result.mergeFirstWeight = 1.0f;
        result.mergeSecondWeight = 0.0f;
        result.mergeThirdWeight = 0.0f;
        result.renderedFeedbackAction = "refined";
        result.renderedFeedbackRefineIntent = refineIntent;
        result.renderedFeedbackRefineReason = refined.reason;
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(
                DevelopRenderedRevisionStageForRefineIntent(refineIntent));
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                refined.reason);
        return true;
    }

    if (!hasRefineIntent &&
        TryApplyRenderedEnsembleMergeToSolve(result, previousToneJson, selectedCandidate)) {
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(result.renderedFeedbackRevisionStage);
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                result.renderedFeedbackRevisionReason);
        return true;
    }

    if (!hasRefineIntent &&
        previousToneJson.value("autoCandidateRenderedMergeSuggested", false)) {
        const std::string mergeFirstId =
            previousToneJson.value("autoCandidateRenderedMergeFirstId", std::string());
        const std::string mergeSecondId =
            previousToneJson.value("autoCandidateRenderedMergeSecondId", std::string());
        if (!mergeFirstId.empty() && !mergeSecondId.empty() && mergeFirstId != mergeSecondId) {
            auto findCandidateById = [&](const std::string& candidateId) {
                return std::find_if(
                    result.candidates.begin(),
                    result.candidates.end(),
                    [&](const DevelopAutoCandidateSolve& candidate) {
                        return candidate.id == candidateId && !candidate.rejected;
                    });
            };

            auto firstIt = findCandidateById(mergeFirstId);
            auto secondIt = findCandidateById(mergeSecondId);
            if (firstIt != result.candidates.end() &&
                secondIt != result.candidates.end() &&
                !firstIt->whiteBalanceProbe &&
                !secondIt->whiteBalanceProbe) {
                float firstRenderedScore =
                    previousToneJson.value("autoCandidateRenderedMergeFirstScore", -1.0f);
                float secondRenderedScore =
                    previousToneJson.value("autoCandidateRenderedMergeSecondScore", -1.0f);
                float renderedScoreFromSolves = -1.0f;
                if (firstRenderedScore < 0.0f &&
                    TryReadRenderedCandidateScore(
                        previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
                        mergeFirstId,
                        renderedScoreFromSolves)) {
                    firstRenderedScore = renderedScoreFromSolves;
                }
                renderedScoreFromSolves = -1.0f;
                if (secondRenderedScore < 0.0f &&
                    TryReadRenderedCandidateScore(
                        previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
                        mergeSecondId,
                        renderedScoreFromSolves)) {
                    secondRenderedScore = renderedScoreFromSolves;
                }

                const float renderedScoreGap =
                    std::fabs(firstRenderedScore - secondRenderedScore);
                const float candidateDistance =
                    DevelopAutoCandidateDistance(firstIt->guidance, secondIt->guidance);
                const float metricDistance =
                    previousToneJson.value("autoCandidateRenderedMergeMetricDistance", 0.0f);
                const bool pairScoresAreStrong =
                    firstRenderedScore >= 0.58f &&
                    secondRenderedScore >= 0.54f &&
                    renderedScoreGap <= 0.16f;
                const bool pairIsMeaningfullyDifferent =
                    candidateDistance >= 0.16f ||
                    metricDistance >= 0.11f;
                if (pairScoresAreStrong && pairIsMeaningfullyDifferent) {
                    const DevelopAutoCandidateSolve firstCandidate = *firstIt;
                    const DevelopAutoCandidateSolve secondCandidate = *secondIt;
                    const float firstWeight = std::clamp(
                        0.50f + (firstRenderedScore - secondRenderedScore) * 0.65f,
                        0.42f,
                        0.62f);
                    DevelopAutoCandidateSolve merged;
                    merged.id = "renderedFeedbackPairMerge";
                    merged.label = "Rendered Pair Merge";
                    merged.reason =
                        "Merged two strong, distinct rendered survivors in authored settings space so the next solve can reconcile their intent instead of blending pixels.";
                    merged.guidance = BlendDevelopAutoCandidateGuidance(
                        firstCandidate.guidance,
                        secondCandidate.guidance,
                        firstWeight);
                    merged.score = std::min(
                        1.0f,
                        std::max(firstCandidate.score, secondCandidate.score) + 0.018f);
                    result.candidates.push_back(merged);
                    result.selectionSource = "renderedMetricsPairMerge";
                    result.authoredGuidance = merged.guidance;
                    result.selectedId = merged.id;
                    result.selectedLabel = merged.label;
                    result.selectedScore = merged.score;
                    ClearDevelopResultWhiteBalanceProbe(result);
                    result.mergeApplied = true;
                    result.mergeFirstId = firstCandidate.id;
                    result.mergeSecondId = secondCandidate.id;
                    result.mergeThirdId.clear();
                    result.mergeFirstWeight = firstWeight;
                    result.mergeSecondWeight = 1.0f - firstWeight;
                    result.mergeThirdWeight = 0.0f;
                    result.renderedFeedbackAction = "merged";
                    result.renderedFeedbackRevisionStage =
                        resolveRenderedFeedbackStage(
                            DevelopRenderedRevisionStageForGuidanceDelta(
                                selectedCandidate.guidance,
                                merged.guidance,
                                merged.id));
                    result.renderedFeedbackRevisionReason =
                        resolveRenderedFeedbackReason(
                            result.renderedFeedbackRevisionStage,
                            "Rendered pair merge affects multiple authored stages; the next pass should validate the merged intent against actual rendered output.");
                    return true;
                }
            }
        }
    }

    const float renderedImprovement =
        selectedRendered ? (bestScore - selectedRenderedScore) : 1.0f;
    const float candidateDistance =
        DevelopAutoCandidateDistance(selectedCandidate.guidance, bestCandidate.guidance);
    const bool canMergeRenderedFeedback =
        selectedRendered &&
        !selectedCandidate.whiteBalanceProbe &&
        !bestCandidate.whiteBalanceProbe &&
        selectedRenderedScore >= 0.50f &&
        renderedImprovement <= 0.12f &&
        candidateDistance >= 0.10f;

    if (canMergeRenderedFeedback) {
        // Modest rendered wins are treated as a combined intent instead of a hard jump.
        const float selectedWeight = std::clamp(
            0.50f - renderedImprovement * 1.15f,
            0.36f,
            0.50f);
        DevelopAutoCandidateSolve merged;
        merged.id = "renderedFeedbackMerge";
        merged.label = "Rendered Feedback Merge";
        merged.reason =
            "Merged the current selected candidate and rendered-best survivor in authored settings space after rendered comparison.";
        merged.guidance = BlendDevelopAutoCandidateGuidance(
            selectedCandidate.guidance,
            bestCandidate.guidance,
            selectedWeight);
        merged.score = std::min(1.0f, std::max(selectedCandidate.score, bestCandidate.score) + 0.015f);
        result.candidates.push_back(merged);
        result.selectionSource = "renderedMetricsMerge";
        result.authoredGuidance = merged.guidance;
        result.selectedId = merged.id;
        result.selectedLabel = merged.label;
        result.selectedScore = merged.score;
        ClearDevelopResultWhiteBalanceProbe(result);
        result.mergeApplied = true;
        result.mergeFirstId = selectedCandidate.id;
        result.mergeSecondId = bestCandidate.id;
        result.mergeThirdId.clear();
        result.mergeFirstWeight = selectedWeight;
        result.mergeSecondWeight = 1.0f - selectedWeight;
        result.mergeThirdWeight = 0.0f;
        result.renderedFeedbackAction = "merged";
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(
                DevelopRenderedRevisionStageForGuidanceDelta(
                    selectedCandidate.guidance,
                    merged.guidance,
                    merged.id));
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                "Rendered selected-vs-best merge affects multiple authored stages; the next pass should validate the merged intent against actual rendered output.");
    } else {
        // Clear rendered wins still adopt the better authored candidate directly.
        result.selectionSource = "renderedMetrics";
        result.authoredGuidance = bestCandidate.guidance;
        result.selectedId = bestCandidate.id;
        result.selectedLabel = bestCandidate.label;
        result.selectedScore = bestCandidate.score;
        SetDevelopResultWhiteBalanceProbe(result, bestCandidate);
        result.renderedFeedbackAction = "adopted";
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(
                DevelopRenderedRevisionStageForGuidanceDelta(
                    selectedCandidate.guidance,
                    bestCandidate.guidance,
                    bestCandidate.id));
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                "Rendered feedback adopted a stronger authored candidate; the next pass should validate the earliest changed stage first.");
        if (result.selectedId != "mergedAutoPick") {
            result.mergeApplied = false;
            result.mergeFirstId.clear();
            result.mergeSecondId.clear();
            result.mergeThirdId.clear();
            result.mergeFirstWeight = 1.0f;
            result.mergeSecondWeight = 0.0f;
            result.mergeThirdWeight = 0.0f;
        }
    }
    return true;
}

DevelopAutoCandidateSolveResult BuildDevelopAutoCandidateSolve(
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    const DevelopToneAutoStats& stats,
    const Raw::RawMetadata& metadata,
    const nlohmann::json& previousToneJson) {
    DevelopAutoCandidateSolveResult result;
    result.candidateContextFingerprint =
        BuildDevelopAutoCandidateContextFingerprint(modeGuidance, intent, subjectImportance, stats);
    const float darkness =
        stats.valid ? std::clamp((0.18f - stats.midtonePercentile) / 0.18f, 0.0f, 1.0f) : 0.0f;
    const float deepShadow =
        stats.valid ? std::clamp((0.06f - stats.shadowPercentile) / 0.06f, 0.0f, 1.0f) : 0.0f;
    const float hdrNeed = SaturateFloat((stats.hdrSpreadEv - 2.8f) / 2.8f);
    const float flatSceneNeed =
        SaturateFloat((2.35f - stats.hdrSpreadEv) / 1.15f) *
        SaturateFloat(1.0f - stats.highlightPressure * 1.35f) *
        SaturateFloat(1.0f - stats.noiseRisk * 0.65f);
    const float shadowRescueNeed = SaturateFloat(darkness * 0.72f + deepShadow * 0.28f);
    const float underBrightBroadHighlightEv = stats.valid && stats.midtonePercentile > 0.08f
        ? std::clamp(std::log2(0.70f / std::max(stats.highlightPercentile, 0.0001f)), 0.0f, 1.25f)
        : 0.0f;
    const float tinySpecularAllowance = stats.valid
        ? SaturateFloat((0.010f - stats.clippingRatio) / 0.010f) *
            SaturateFloat((0.72f - stats.highlightPressure) / 0.72f)
        : 0.0f;
    const DevelopDynamicRangeRegionEvidence regionEvidence =
        ResolveDevelopDynamicRangeRegionEvidence(previousToneJson);
    const DevelopDynamicRangeStrategy dynamicRangeStrategy =
        ResolveDevelopDynamicRangeStrategy(
            intent,
            modeGuidance,
            stats,
            regionEvidence,
            darkness,
            shadowRescueNeed,
            hdrNeed,
            flatSceneNeed,
            tinySpecularAllowance);
    result.dynamicRangeStrategy = DevelopDynamicRangeStrategyToJson(dynamicRangeStrategy);
    const DevelopSubjectSceneIntent subjectSceneIntent =
        ResolveDevelopSubjectSceneIntent(
            intent,
            modeGuidance,
            subjectImportance,
            stats,
            regionEvidence);
    result.subjectSceneIntent = DevelopSubjectSceneIntentToJson(subjectSceneIntent);
    const DevelopContinuationCandidateBiasProfile continuationBias =
        ResolveDevelopContinuationCandidateBiasProfile(previousToneJson);
    result.continuationBiasActive = continuationBias.active;
    result.continuationBiasReason = continuationBias.reason;
    result.continuationBiasDecision = continuationBias.decision;
    result.continuationBiasStage = continuationBias.stageFocus;
    result.continuationBiasRefineIntent = continuationBias.refineIntent;
    result.continuationExpansionEligible = continuationBias.active;
    result.continuationExpansionReason = continuationBias.reason;
    result.continuationExpansionStage = continuationBias.stageFocus;
    result.continuationExpansionRefineIntent = continuationBias.refineIntent;

    auto addCandidateCore = [&](std::string id,
                                std::string label,
                                std::string reason,
                                EditorNodeGraph::DevelopAutoGuidance guidance,
                                bool continuationExpansionCandidate,
                                std::string continuationExpansionReason) {
        DevelopAutoCandidateSolve candidate;
        candidate.id = std::move(id);
        candidate.label = std::move(label);
        candidate.reason = std::move(reason);
        candidate.guidance = guidance;
        candidate.guidance.intent = intent;
        candidate.continuationExpansionCandidate = continuationExpansionCandidate;
        if (candidate.continuationExpansionCandidate) {
            candidate.continuationExpansionReason = std::move(continuationExpansionReason);
            candidate.continuationExpansionStage = continuationBias.stageFocus;
            candidate.continuationExpansionRefineIntent = continuationBias.refineIntent;
        }
        Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
        candidate.whiteBalanceProbe =
            TryResolveDevelopWhiteBalanceProbeMode(candidate.id, whiteBalanceMode);
        candidate.whiteBalanceMode = whiteBalanceMode;
        candidate.guidanceFingerprint =
            BuildDevelopAutoCandidateGuidanceFingerprint(
                candidate.id,
                candidate.guidance,
                &subjectImportance);
        candidate.score = ScoreDevelopAutoCandidate(
            candidate.id,
            intent,
            modeGuidance,
            stats,
            regionEvidence,
            dynamicRangeStrategy,
            subjectSceneIntent,
            darkness,
            shadowRescueNeed,
            hdrNeed,
            flatSceneNeed,
            underBrightBroadHighlightEv);
        if (ApplyDevelopContinuationCandidateBias(candidate, continuationBias)) {
            ++result.continuationBiasAppliedCount;
        }
        candidate.scoreComponents = BuildDevelopAutoCandidateScoreComponents(
            candidate,
            modeGuidance,
            intent,
            stats,
            regionEvidence,
            dynamicRangeStrategy,
            subjectSceneIntent,
            darkness,
            shadowRescueNeed,
            hdrNeed,
            flatSceneNeed,
            underBrightBroadHighlightEv);
        RejectDevelopAutoCandidateForDamage(candidate, modeGuidance, stats, intent);
        std::string rememberedReason;
        if (candidate.id != "base" &&
            TryReadRememberedCandidateRejection(
                previousToneJson,
                result.candidateContextFingerprint,
                candidate.id,
                rememberedReason)) {
            candidate.rejected = true;
            candidate.rememberedRejection = true;
            candidate.rejectReason = "Rejected from solver memory: " + rememberedReason;
            ++result.rejectedMemorySuppressionCount;
        }
        if (!candidate.rejected && candidate.id != "base" &&
            TryReadRememberedRenderedCandidateRejection(
                previousToneJson,
                candidate.guidanceFingerprint,
                candidate.id,
                rememberedReason)) {
            candidate.rejected = true;
            candidate.rememberedRejection = true;
            candidate.renderedMemoryRejected = true;
            candidate.rejectReason = "Rejected from rendered memory: " + rememberedReason;
            ++result.renderedRejectedMemorySuppressionCount;
        }
        result.candidates.push_back(std::move(candidate));
    };
    auto addCandidate = [&](std::string id,
                            std::string label,
                            std::string reason,
                            EditorNodeGraph::DevelopAutoGuidance guidance) {
        addCandidateCore(
            std::move(id),
            std::move(label),
            std::move(reason),
            guidance,
            false,
            std::string());
    };

    addCandidate(
        "base",
        "Base Solve",
        "Mode-aware solve using the current Auto intent as the starting point.",
        modeGuidance);

    if (stats.valid) {
        const bool rangeIntent =
            intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
        const bool flatIntent =
            intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
        const float strategyHighlightBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapHighlightPriority - 0.5f);
        const float strategyShadowBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapShadowVisibility - 0.5f);
        const float strategyNaturalContrastBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapNaturalContrast - 0.5f);
        const float strategyVisibleRangeBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapVisibleRange - 0.5f);
        const float subjectRevealIntent = SaturateFloat(
            subjectSceneIntent.subjectPriority * 0.26f +
            subjectSceneIntent.improveReadability * 0.24f +
            subjectSceneIntent.readabilityPressure * 0.14f +
            subjectSceneIntent.automaticConfidence * 0.08f +
            std::max(0.0f, subjectSceneIntent.userSubjectSceneBias) * 0.16f +
            std::max(0.0f, subjectSceneIntent.userMoodReadabilityBias) * 0.14f -
            regionEvidence.shadowNoiseLiftRisk * 0.08f -
            regionEvidence.localHaloRisk * 0.04f);
        const float sceneMoodIntent = SaturateFloat(
            subjectSceneIntent.sceneIntegrity * 0.24f +
            subjectSceneIntent.preserveMood * 0.26f +
            subjectSceneIntent.moodPreservationPressure * 0.14f +
            darkness * 0.10f +
            stats.noiseRisk * 0.06f +
            std::max(0.0f, -subjectSceneIntent.userSubjectSceneBias) * 0.12f +
            std::max(0.0f, -subjectSceneIntent.userMoodReadabilityBias) * 0.16f);
        const bool subjectReadableCandidateNeeded =
            subjectSceneIntent.subjectPriority > 0.56f &&
            subjectRevealIntent > 0.42f &&
            (subjectSceneIntent.userGuidanceActive ||
                subjectSceneIntent.automaticConfidence > 0.46f ||
                subjectSceneIntent.readabilityPressure > 0.30f) &&
            regionEvidence.shadowNoiseLiftRisk < 0.82f;
        const bool sceneMoodCandidateNeeded =
            sceneMoodIntent > 0.46f &&
            (subjectSceneIntent.userGuidanceActive ||
                subjectSceneIntent.preserveMood > 0.60f ||
                darkness > 0.22f) &&
            (subjectSceneIntent.sceneIntegrity > 0.52f ||
                subjectSceneIntent.userSubjectSceneBias < -0.20f ||
                subjectSceneIntent.userMoodReadabilityBias < -0.20f);

        auto addModeNeighborCandidate = [&](std::string id,
                                            std::string label,
                                            std::string reason,
                                            EditorNodeGraph::DevelopAutoGuidance guidance) {
            addCandidate(std::move(id), std::move(label), std::move(reason), guidance);
        };

        // Mode-neighbor probes are nearby intent vectors, not presets. They let
        // rendered feedback compare the current mode against plausible adjacent
        // tradeoffs without changing the selected Auto mode.
        switch (intent) {
            case EditorNodeGraph::DevelopAutoIntent::NaturalFinished:
                if (hdrNeed > 0.12f || stats.highlightPressure > 0.38f) {
                    addModeNeighborCandidate(
                        "modeNeighborNaturalMoreRange",
                        "Natural More Range",
                        "Test the neighboring Natural/Range intent when broad highlights or HDR spread make the default natural solve uncertain.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            -0.06f,
                            0.30f,
                            0.14f * (1.0f - stats.noiseRisk * 0.35f),
                            0.22f,
                            -0.05f,
                            -0.18f));
                }
                if (shadowRescueNeed > 0.18f || underBrightBroadHighlightEv > 0.12f) {
                    addModeNeighborCandidate(
                        "modeNeighborNaturalBrighterMids",
                        "Natural Brighter Mids",
                        "Test the neighboring Natural/Bright intent when useful mids may be landing too low.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            0.15f,
                            0.07f,
                            0.13f,
                            0.09f,
                            0.00f,
                            -0.04f));
                }
                if (flatSceneNeed > 0.16f || (stats.textureConfidence > 0.55f && stats.noiseRisk < 0.68f)) {
                    addModeNeighborCandidate(
                        "modeNeighborNaturalPunchier",
                        "Natural More Contrast",
                        "Test the neighboring Natural/Punchy intent when the image may support more separation without fake HDR.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            0.00f,
                            -0.18f,
                            -0.14f,
                            0.04f,
                            0.10f,
                            0.26f));
                }
                break;
            case EditorNodeGraph::DevelopAutoIntent::BrightNatural:
                addModeNeighborCandidate(
                    "modeNeighborBrightHighlightSafe",
                    "Bright Highlight Safe",
                    "Test a neighboring Bright/Range intent so a brighter result can keep believable highlight headroom.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        -0.05f,
                        0.22f,
                        0.04f,
                        0.28f,
                        -0.04f,
                        -0.09f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::DarkNatural:
                addModeNeighborCandidate(
                    "modeNeighborDarkReadableMids",
                    "Dark Readable Mids",
                    "Test a neighboring Dark/Natural intent that keeps mood while checking whether important mids need a little readability.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        0.08f,
                        0.10f,
                        0.14f,
                        0.05f,
                        -0.01f,
                        -0.03f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast:
                addModeNeighborCandidate(
                    "modeNeighborPunchySaferRange",
                    "Punchy Safer Range",
                    "Test a neighboring Punchy/Range intent that preserves separation while backing away from harsh highlight or shadow pressure.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        -0.03f,
                        0.22f,
                        0.08f,
                        0.22f,
                        -0.04f,
                        -0.14f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail:
                addModeNeighborCandidate(
                    "modeNeighborRangeNaturalShape",
                    "Range Natural Shape",
                    "Test a neighboring Range/Natural intent that gives back some contrast if maximum range starts to look too flat.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        0.02f,
                        -0.24f,
                        -0.12f,
                        -0.05f,
                        0.06f,
                        0.18f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::FlatEditingBase:
                addModeNeighborCandidate(
                    "modeNeighborFlatNaturalShape",
                    "Flat Natural Shape",
                    "Test a neighboring Flat/Natural intent so the editing base keeps enough tonal shape to remain useful.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        0.02f,
                        -0.18f,
                        -0.08f,
                        -0.02f,
                        0.05f,
                        0.16f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::CleanBase:
                if (stats.textureConfidence > 0.42f) {
                    addModeNeighborCandidate(
                        "modeNeighborCleanTextureCheck",
                        "Clean Texture Check",
                        "Test a neighboring Clean/Natural intent so the clean base does not erase real texture.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            0.00f,
                            0.04f,
                            0.05f,
                            0.02f,
                            0.05f,
                            0.13f));
                }
                break;
        }

        const bool hasCameraWhiteBalance =
            HasMeaningfulRawWhiteBalanceMetadata(metadata);
        const bool hasDaylightWhiteBalance =
            HasMeaningfulRawDaylightWhiteBalanceMetadata(metadata);
        const std::array<float, 3> neutralWhiteBalance = { 1.0f, 1.0f, 1.0f };
        const std::array<float, 3> cameraWhiteBalance =
            hasCameraWhiteBalance
                ? NormalizeRawWhiteBalanceTriplet(metadata.cameraWhiteBalance)
                : neutralWhiteBalance;
        const std::array<float, 3> daylightWhiteBalance =
            hasDaylightWhiteBalance
                ? NormalizeRawWhiteBalanceTriplet(metadata.daylightWhiteBalance)
                : neutralWhiteBalance;
        const float cameraDaylightDistance =
            hasCameraWhiteBalance && hasDaylightWhiteBalance
                ? RawWhiteBalanceTripletDistance(cameraWhiteBalance, daylightWhiteBalance)
                : 0.0f;
        const float cameraNeutralDistance =
            hasCameraWhiteBalance
                ? RawWhiteBalanceTripletDistance(cameraWhiteBalance, neutralWhiteBalance)
                : 0.0f;

        if (hasCameraWhiteBalance &&
            hasDaylightWhiteBalance &&
            cameraDaylightDistance > 0.18f) {
            addCandidate(
                "wbDaylightCorrection",
                "WB Daylight Correction",
                "Test a daylight-oriented RAW white balance when camera and daylight metadata meaningfully disagree.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.03f,
                    0.00f,
                    0.04f,
                    -0.01f,
                    -0.03f));
        }

        const bool neutralCorrectionUseful =
            hasCameraWhiteBalance &&
            cameraNeutralDistance > 0.42f &&
            (intent == EditorNodeGraph::DevelopAutoIntent::CleanBase ||
                intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase ||
                intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail ||
                cameraDaylightDistance > 0.26f);
        if (neutralCorrectionUseful) {
            addCandidate(
                "wbNeutralCorrection",
                "WB Neutral Correction",
                "Test a conservative neutral RAW white balance for technically clean or editing-oriented output.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.02f,
                    0.00f,
                    0.03f,
                    -0.02f,
                    -0.04f));
        }

        // Candidate families are authored-state probes; they do not render or blend pixels here.
        addCandidate(
            "protectHighlights",
            "Protect Highlights",
            "Lower visible placement and strengthen highlight protection for broad bright-region risk.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                -0.08f - stats.highlightPressure * 0.12f,
                0.18f,
                0.06f,
                0.34f,
                -0.04f,
                -0.08f));

        if (hdrNeed > 0.18f || stats.highlightPressure > 0.42f || underBrightBroadHighlightEv > 0.18f) {
            addCandidate(
                "highlightProtectedMids",
                "Highlight-Protected Mids",
                "Test a lower global/RAW placement with local midtone support so highlights keep headroom without burying useful mids.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.18f - stats.highlightPressure * 0.12f,
                    0.34f,
                    0.28f * (1.0f - stats.noiseRisk * 0.35f),
                    0.38f,
                    -0.05f,
                    -0.16f));
        }

        if (dynamicRangeStrategy.broadHighlightGuardNeed > 0.34f ||
            dynamicRangeStrategy.strategyMapHighlightPriority > 0.66f ||
            dynamicRangeStrategy.id == "broadHighlightGuard" ||
            (stats.highlightPressure > 0.58f &&
                stats.clippingRatio > 0.006f &&
                !dynamicRangeStrategy.smallSpecularClippingAllowed)) {
            addCandidate(
                "broadHighlightGuard",
                "Broad Highlight Guard",
                "Test scene-prep highlight compression for broad bright regions while keeping RAW placement stable; this preserves visible range and does not recover fully clipped detail.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f + strategyVisibleRangeBias * 0.08f,
                    0.02f,
                    0.34f + strategyHighlightBias * 0.08f,
                    -0.06f,
                    -0.12f));
        }

        addCandidate(
            "brighterMids",
            "Brighter Mids",
            "Raise important midtone placement while keeping highlight guard active.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                0.10f + shadowRescueNeed * 0.10f,
                0.08f,
                0.16f,
                0.10f,
                0.00f,
                -0.04f));

        addCandidate(
            "maximumRange",
            "More Range",
            "Fit more highlight and shadow information into the visible range without claiming clipped-data recovery.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                -stats.highlightPressure * 0.06f,
                0.40f + strategyVisibleRangeBias * 0.12f,
                0.22f * (1.0f - stats.noiseRisk * 0.45f) + strategyShadowBias * 0.07f,
                0.32f + strategyHighlightBias * 0.08f,
                -0.06f,
                -0.20f - strategyVisibleRangeBias * 0.06f));

        if (regionEvidence.valid &&
            (regionEvidence.localRangeConflict > 0.40f ||
                regionEvidence.localEvConflict > 0.38f ||
                regionEvidence.localHaloRisk > 0.30f ||
                regionEvidence.localDamageRiskPeak > 0.62f ||
                (dynamicRangeStrategy.strategyMapVisibleRange > 0.68f &&
                    regionEvidence.localEvConflict > 0.28f))) {
            addCandidate(
                "localRangeGuard",
                "Local Range Guard",
                "Use rendered regional evidence to test gentler scene-prep range shaping where local highlight, shadow, or halo pressure is concentrated.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f,
                    0.08f * (1.0f - regionEvidence.shadowNoiseLiftRisk * 0.55f),
                    0.20f,
                    -0.04f,
                    -0.10f));
        }

        if (dynamicRangeStrategy.localHaloGuardNeed > 0.32f ||
            dynamicRangeStrategy.id == "haloSafeLocalRange" ||
            (regionEvidence.valid &&
                regionEvidence.localHaloRisk > 0.34f)) {
            addCandidate(
                "haloSafeLocalRange",
                "Halo-Safe Local Range",
                "Test safer scene-prep local exposure with stronger halo and smooth-gradient guardrails where rendered regional evidence warns about edge glow or artificial relighting.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.02f,
                    -0.08f,
                    -0.08f,
                    0.12f,
                    -0.04f,
                    -0.06f));
        }

        if (dynamicRangeStrategy.shadowReadabilityLiftNeed > 0.34f ||
            dynamicRangeStrategy.strategyMapShadowVisibility > 0.66f ||
            dynamicRangeStrategy.id == "shadowReadabilityLift" ||
            (shadowRescueNeed > 0.42f &&
                stats.noiseRisk < 0.48f &&
                regionEvidence.shadowNoiseLiftRisk < 0.52f)) {
            addCandidate(
                "shadowReadabilityLift",
                "Shadow Readability Lift",
                "Test scene-prep shadow and midtone opening for readable shadows while keeping RAW placement stable and noise guardrails active.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f + strategyVisibleRangeBias * 0.05f,
                    0.30f + strategyShadowBias * 0.08f,
                    0.08f,
                    -0.03f,
                    -0.08f));
        }

        if (subjectReadableCandidateNeeded) {
            addCandidate(
                "subjectReadableMids",
                "Subject Readable Mids",
                "Test a subject-priority Scene Prep branch that opens likely or user-marked important mids while keeping highlight, noise, and halo guardrails active.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.04f + std::max(0.0f, subjectSceneIntent.userMoodReadabilityBias) * 0.04f,
                    0.14f + subjectRevealIntent * 0.10f,
                    0.18f + subjectRevealIntent * 0.10f,
                    0.08f + subjectSceneIntent.protectionPressure * 0.05f,
                    -0.02f,
                    -0.06f));
        }

        if (sceneMoodCandidateNeeded) {
            addCandidate(
                "sceneMoodPreservation",
                "Scene Mood Preservation",
                "Test a scene-integrity branch that keeps low-key mood or silhouette intent from being forced into gray midtones; subject importance remains a bias, not a command.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    -0.04f,
                    -0.18f - sceneMoodIntent * 0.06f,
                    0.08f + subjectSceneIntent.protectionPressure * 0.04f,
                    0.03f,
                    0.08f));
        }

        if (dynamicRangeStrategy.shadowNoiseFloorNeed > 0.34f ||
            dynamicRangeStrategy.id == "shadowNoiseFloor" ||
            (stats.noiseRisk > 0.58f && shadowRescueNeed > 0.18f)) {
            addCandidate(
                "shadowNoiseFloor",
                "Shadow Noise Floor",
                "Test holding noisy or low-value dark regions darker with Scene Prep limits instead of lifting them into gray noise.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    -0.10f,
                    -0.24f,
                    0.10f,
                    0.02f,
                    0.07f));
        }

        addCandidate(
            "preserveMood",
            "Preserve Mood",
            "Keep darker scene hierarchy and avoid forcing low-key images into gray mids.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                -0.18f,
                -0.06f,
                -0.20f,
                0.06f,
                0.06f,
                0.10f));

        addCandidate(
            "strongerContrast",
            "More Contrast",
            "Restore separation and endpoints when the scene can support a punchier finish.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                0.00f,
                -0.12f,
                -0.10f,
                0.03f,
                0.10f,
                0.28f));

        if (hdrNeed > 0.18f || stats.highlightPressure > 0.34f) {
            addCandidate(
                "toneSofterRolloff",
                "Softer Highlight Rolloff",
                "Test a finish-tone shoulder that compresses visible highlights more gently without claiming clipped-data recovery.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.08f,
                    0.02f,
                    0.16f,
                    -0.18f,
                    -0.18f));
        }

        if (dynamicRangeStrategy.brightHighlightRolloffNeed > 0.32f ||
            dynamicRangeStrategy.id == "brightHighlightRolloff") {
            addCandidate(
                "brightHighlightRolloff",
                "Bright Highlight Rolloff",
                "Test a downstream highlight shoulder that preserves the feeling of bright light while still smoothing rolloff; this shapes visible data and does not recover fully clipped detail.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.01f,
                    -0.05f,
                    -0.03f,
                    0.10f,
                    0.24f,
                    0.08f));
        }

        if (dynamicRangeStrategy.highlightBrightnessAnchorNeed > 0.30f ||
            dynamicRangeStrategy.id == "luminousHighlightAnchor" ||
            (regionEvidence.valid &&
                regionEvidence.brightnessHierarchyRisk > 0.38f &&
                dynamicRangeStrategy.broadHighlightGuardNeed > 0.30f)) {
            addCandidate(
                "luminousHighlightAnchor",
                "Luminous Highlight Anchor",
                "Test downstream highlight separation so protected broad highlights stay luminous instead of flattening toward gray; this shapes visible range and does not recover clipped detail.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.04f,
                    0.02f,
                    0.30f,
                    0.16f));
        }

        if (dynamicRangeStrategy.naturalContrastGuardNeed > 0.32f ||
            (dynamicRangeStrategy.strategyMapNaturalContrast > 0.66f &&
                !rangeIntent &&
                !flatIntent) ||
            dynamicRangeStrategy.id == "naturalContrastGuard" ||
            (regionEvidence.valid &&
                (regionEvidence.flatGrayRisk > 0.40f ||
                    regionEvidence.brightnessHierarchyRisk > 0.42f))) {
            addCandidate(
                "naturalContrastGuard",
                "Natural Contrast Guard",
                "Test a downstream finish-tone shape that restores believable separation when range compression or flat-gray risk threatens the lighting hierarchy.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.12f,
                    -0.06f,
                    0.02f,
                    0.14f + strategyNaturalContrastBias * 0.05f,
                    0.24f + strategyNaturalContrastBias * 0.08f));
        }

        if ((dynamicRangeStrategy.smallSpecularClippingAllowed &&
                dynamicRangeStrategy.specularHighlightToleranceNeed > 0.28f) ||
            dynamicRangeStrategy.id == "specularHighlightTolerance") {
            addCandidate(
                "specularHighlightTolerance",
                "Specular Highlight Tolerance",
                "Test a finish-tone shape that lets tiny specular cores stay bright while preserving smooth rolloff around them; this is not clipped-data recovery.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.03f,
                    -0.16f,
                    -0.04f,
                    -0.16f,
                    0.32f,
                    0.14f));
        }

        if (flatSceneNeed > 0.14f ||
            stats.textureConfidence > 0.50f ||
            intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast ||
            dynamicRangeStrategy.strategyMapNaturalContrast > 0.68f) {
            addCandidate(
                "tonePunchierShape",
                "Punchier Tone Shape",
                "Test a downstream tone shape with stronger separation while keeping upstream RAW and scene prep stable.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.06f,
                    0.02f,
                    0.12f,
                    0.24f));
        }

        if (intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase ||
            intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail ||
            hdrNeed > 0.30f ||
            dynamicRangeStrategy.strategyMapVisibleRange > 0.68f) {
            addCandidate(
                "toneFlatterEditing",
                "Flatter Editing Tone",
                "Test a finish-tone shape that leaves more visible range and gentler endpoints for later Manual editing.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f + strategyVisibleRangeBias * 0.08f,
                    0.08f,
                    0.16f,
                    -0.10f,
                    -0.26f));
        }

        if (intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural ||
            darkness > 0.24f ||
            stats.noiseRisk > 0.55f) {
            addCandidate(
                "toneDarkerToe",
                "Darker Shadow Toe",
                "Test a darker downstream shadow toe so low-key mood or noisy dark regions are not forced into gray mids.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.05f,
                    -0.05f,
                    -0.20f,
                    0.04f,
                    0.06f,
                    0.14f));
        }

        addCandidate(
            "cleanShadows",
            "Cleaner Shadows",
            "Reduce shadow-opening pressure when noise risk is high so texture stays natural.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                0.03f,
                -0.08f,
                -0.12f,
                0.12f,
                -0.02f,
                -0.06f));

        if (stats.textureConfidence > 0.45f && stats.noiseRisk < 0.82f) {
            addCandidate(
                "preserveTexture",
                "Preserve Texture",
                "Test a texture-preserving cleanup/detail balance when image stats show real texture that could be smeared.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.01f,
                    -0.04f,
                    -0.05f,
                    0.05f,
                    0.04f,
                    0.12f));
        }
    }

    auto addContinuationExpansionCandidate = [&](std::string id,
                                                 std::string label,
                                                 std::string reason,
                                                 EditorNodeGraph::DevelopAutoGuidance guidance) {
        if (!continuationBias.active ||
            !stats.valid ||
            HasDevelopAutoCandidateId(result.candidates, id)) {
            return false;
        }
        const std::string expansionReason =
            "Rendered continuation requested another " +
            (continuationBias.stageFocus.empty() ? std::string("multi-stage") : continuationBias.stageFocus) +
            " validation pass, so this candidate family was generated even though the first-pass stats gate did not require it.";
        addCandidateCore(
            std::move(id),
            std::move(label),
            std::move(reason),
            guidance,
            true,
            expansionReason);
        ++result.continuationExpansionAddedCount;
        return true;
    };

    if (continuationBias.active && stats.valid) {
        const bool wantsRawGlobal =
            continuationBias.stageFocus == "rawGlobal" ||
            continuationBias.refineIntent == "protectHighlights";
        const bool wantsScenePrep =
            continuationBias.stageFocus == "scenePrep" ||
            continuationBias.refineIntent == "brightenMids" ||
            continuationBias.refineIntent == "openShadows";
        const bool wantsFinishTone =
            continuationBias.stageFocus == "finishTone" ||
            continuationBias.refineIntent == "addContrast";
        const bool wantsRawCleanup =
            continuationBias.stageFocus == "rawCleanup" ||
            continuationBias.refineIntent == "cleanShadows" ||
            continuationBias.refineIntent == "preserveTexture";

        if (wantsRawGlobal) {
            addContinuationExpansionCandidate(
                "highlightProtectedMids",
                "Highlight-Protected Mids",
                "Re-test a lower global/RAW placement with local midtone support because rendered continuation is focused on RAW/global highlight placement.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.16f - stats.highlightPressure * 0.10f,
                    0.32f,
                    0.24f * (1.0f - stats.noiseRisk * 0.35f),
                    0.36f,
                    -0.05f,
                    -0.15f));
        }

        if (wantsScenePrep) {
            addContinuationExpansionCandidate(
                "broadHighlightGuard",
                "Broad Highlight Guard",
                "Re-test broad highlight scene-prep protection because rendered continuation is focused on local exposure/range control.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f,
                    0.02f,
                    0.34f,
                    -0.06f,
                    -0.12f));
            addContinuationExpansionCandidate(
                "localRangeGuard",
                "Local Range Guard",
                "Re-test gentler scene-prep range shaping because rendered continuation is focused on regional local exposure pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f,
                    0.08f * (1.0f - regionEvidence.shadowNoiseLiftRisk * 0.55f),
                    0.20f,
                    -0.04f,
                    -0.10f));
            addContinuationExpansionCandidate(
                "haloSafeLocalRange",
                "Halo-Safe Local Range",
                "Re-test halo-safe scene-prep range shaping because rendered continuation is focused on local exposure safety.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.02f,
                    -0.08f,
                    -0.08f,
                    0.12f,
                    -0.04f,
                    -0.06f));
            addContinuationExpansionCandidate(
                "shadowReadabilityLift",
                "Shadow Readability Lift",
                "Re-test readable-shadow scene-prep opening because rendered continuation is focused on local exposure and shadow readability.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f,
                    0.30f,
                    0.08f,
                    -0.03f,
                    -0.08f));
            addContinuationExpansionCandidate(
                "shadowNoiseFloor",
                "Shadow Noise Floor",
                "Re-test a guarded shadow floor because rendered continuation is focused on scene-prep local exposure and shadow/noise pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    -0.10f,
                    -0.24f,
                    0.10f,
                    0.02f,
                    0.07f));
            addContinuationExpansionCandidate(
                "modeNeighborNaturalMoreRange",
                "Natural More Range",
                "Re-test a nearby range/readability intent because rendered continuation is focused on scene-prep local exposure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.28f,
                    0.16f * (1.0f - stats.noiseRisk * 0.35f),
                    0.18f,
                    -0.04f,
                    -0.14f));
        }

        if (wantsFinishTone) {
            addContinuationExpansionCandidate(
                "toneSofterRolloff",
                "Softer Highlight Rolloff",
                "Re-test a gentler downstream highlight shoulder because rendered continuation is focused on finish tone.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.08f,
                    0.02f,
                    0.16f,
                    -0.18f,
                    -0.18f));
            addContinuationExpansionCandidate(
                "brightHighlightRolloff",
                "Bright Highlight Rolloff",
                "Re-test a downstream highlight shoulder that keeps bright regions luminous because rendered continuation is focused on finish tone.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.01f,
                    -0.05f,
                    -0.03f,
                    0.10f,
                    0.24f,
                    0.08f));
            addContinuationExpansionCandidate(
                "luminousHighlightAnchor",
                "Luminous Highlight Anchor",
                "Re-test downstream highlight separation because rendered continuation is focused on finish-tone highlight brightness feeling.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.04f,
                    0.02f,
                    0.30f,
                    0.16f));
            addContinuationExpansionCandidate(
                "specularHighlightTolerance",
                "Specular Highlight Tolerance",
                "Re-test tiny-specular highlight tolerance because rendered continuation is focused on finish-tone highlight shape.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.03f,
                    -0.16f,
                    -0.04f,
                    -0.16f,
                    0.32f,
                    0.14f));
            addContinuationExpansionCandidate(
                "naturalContrastGuard",
                "Natural Contrast Guard",
                "Re-test natural contrast separation because rendered continuation is focused on finish-tone hierarchy.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.12f,
                    -0.06f,
                    0.02f,
                    0.14f,
                    0.24f));
            addContinuationExpansionCandidate(
                "tonePunchierShape",
                "Punchier Tone Shape",
                "Re-test a stronger downstream tone shape because rendered continuation is focused on finish tone.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.06f,
                    0.02f,
                    0.12f,
                    0.24f));
            addContinuationExpansionCandidate(
                "toneFlatterEditing",
                "Flatter Editing Tone",
                "Re-test a gentler downstream tone shape because rendered continuation needs another finish-tone comparison.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f,
                    0.08f,
                    0.16f,
                    -0.10f,
                    -0.26f));
            addContinuationExpansionCandidate(
                "toneDarkerToe",
                "Darker Shadow Toe",
                "Re-test downstream shadow toe placement because rendered continuation needs another finish-tone comparison.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.05f,
                    -0.05f,
                    -0.20f,
                    0.04f,
                    0.06f,
                    0.14f));
        }

        if (wantsRawCleanup) {
            addContinuationExpansionCandidate(
                "preserveTexture",
                "Preserve Texture",
                "Re-test a texture-preserving cleanup/detail balance because rendered continuation is focused on RAW cleanup.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.01f,
                    -0.04f,
                    -0.05f,
                    0.05f,
                    0.04f,
                    0.12f));
        }
    }

    const std::string renderedRefineIntent =
        previousToneJson.value("autoCandidateRenderedRefineIntent", std::string());
    const std::string renderedRefineReason =
        previousToneJson.value("autoCandidateRenderedRefineReason", std::string());
    const bool renderedFeedbackIterationActive =
        previousToneJson.value("autoCandidateRenderedFeedbackPass", 0) > 0 ||
        previousToneJson.value("autoCandidateRenderedFeedbackApplied", false) ||
        !renderedRefineIntent.empty();
    auto addRenderedLocalCandidate = [&](std::string id,
                                         std::string label,
                                         std::string fallbackReason,
                                         EditorNodeGraph::DevelopAutoGuidance guidance) {
        if (HasDevelopAutoCandidateId(result.candidates, id)) {
            return;
        }
        const std::string reason = renderedRefineReason.empty()
            ? fallbackReason
            : (renderedRefineReason + " Generated a rendered-local candidate family from that mismatch.");
        addCandidate(std::move(id), std::move(label), reason, guidance);
    };
    if (renderedFeedbackIterationActive) {
        if (renderedRefineIntent == "brightenMids") {
            addRenderedLocalCandidate(
                "renderedLocalBrightenMids",
                "Rendered Local Brighten Mids",
                "Rendered local metrics asked for brighter important mids without broad highlight pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.11f,
                    0.09f,
                    0.12f,
                    0.06f,
                    0.00f,
                    -0.04f));
        } else if (renderedRefineIntent == "openShadows") {
            addRenderedLocalCandidate(
                "renderedLocalShadowOpening",
                "Rendered Local Shadow Opening",
                "Rendered local metrics asked for opening crowded local shadows while avoiding a global flat lift.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.04f,
                    0.13f,
                    0.20f,
                    0.08f,
                    -0.02f,
                    -0.05f));
        } else if (renderedRefineIntent == "protectHighlights") {
            addRenderedLocalCandidate(
                "renderedLocalHighlightRestraint",
                "Rendered Local Highlight Restraint",
                "Rendered local metrics asked for more highlight/local exposure restraint without claiming clipped-data recovery.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.10f,
                    0.19f,
                    0.04f,
                    0.30f,
                    -0.06f,
                    -0.09f));
        } else if (renderedRefineIntent == "addContrast") {
            addRenderedLocalCandidate(
                "renderedLocalContrastShape",
                "Rendered Local Contrast Shape",
                "Rendered local metrics asked for more regional separation without broad clipping pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.06f,
                    0.02f,
                    0.07f,
                    0.19f));
        } else if (renderedRefineIntent == "cleanShadows") {
            addRenderedLocalCandidate(
                "renderedLocalCleanShadows",
                "Rendered Local Cleaner Shadows",
                "Rendered metrics asked for a cleaner shadow/detail balance instead of more shadow lift.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.01f,
                    -0.07f,
                    -0.13f,
                    0.10f,
                    -0.02f,
                    -0.07f));
        } else if (renderedRefineIntent == "preserveTexture") {
            addRenderedLocalCandidate(
                "renderedLocalPreserveTexture",
                "Rendered Local Preserve Texture",
                "Rendered metrics asked for a texture-preserving cleanup balance rather than a smoother result.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.01f,
                    -0.04f,
                    -0.04f,
                    0.04f,
                    0.05f,
                    0.11f));
        }
    }

    if (renderedFeedbackIterationActive) {
        auto addPreviousCandidate = [&](const std::string& candidateId,
                                        const std::string& fallbackLabel,
                                        const std::string& carryReason) {
            if (HasDevelopAutoCandidateId(result.candidates, candidateId)) {
                return false;
            }

            DevelopAutoCandidateSolve previousCandidate;
            if (!TryReadDevelopAutoCandidateFromToneJson(previousToneJson, candidateId, previousCandidate)) {
                return false;
            }

            previousCandidate.label = previousCandidate.label.empty() ? fallbackLabel : previousCandidate.label;
            previousCandidate.guidance.intent = intent;
            previousCandidate.guidanceFingerprint =
                BuildDevelopAutoCandidateGuidanceFingerprint(
                    previousCandidate.id,
                    previousCandidate.guidance,
                    &subjectImportance);
            previousCandidate.reason = carryReason;
            if (ApplyDevelopContinuationCandidateBias(previousCandidate, continuationBias)) {
                ++result.continuationBiasAppliedCount;
            }
            result.candidates.push_back(std::move(previousCandidate));
            ++result.renderedFeedbackCarriedForwardCount;
            return true;
        };
        addPreviousCandidate(
            previousToneJson.value("autoCandidateSelectedId", std::string()),
            "Previous Auto Pick",
            "Preserved the prior authored candidate so rendered feedback can compare and converge over multiple passes.");
        addPreviousCandidate(
            previousToneJson.value("autoCandidateRenderedBestId", std::string()),
            "Previous Rendered Best",
            "Preserved the prior authored rendered-best candidate so rendered feedback can compare and converge over multiple passes.");
        addPreviousCandidate(
            previousToneJson.value("autoCandidateRenderedMergeFirstId", std::string()),
            "Previous Rendered Merge Source",
            "Preserved the prior authored candidate from a rendered pair-merge source so the next solve can combine actual rendered survivors.");
        addPreviousCandidate(
            previousToneJson.value("autoCandidateRenderedMergeSecondId", std::string()),
            "Previous Rendered Merge Source",
            "Preserved the prior authored candidate from a rendered pair-merge source so the next solve can combine actual rendered survivors.");
        for (const std::string& survivorId : CollectDevelopRenderedSurvivorCandidateIdsForCarryForward(previousToneJson, 4)) {
            addPreviousCandidate(
                survivorId,
                "Previous Rendered Survivor",
                "Preserved the prior authored candidate from an actual rendered survivor so candidate convergence can compare more than only the selected and rendered-best states.");
        }
    }
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        DevelopAutoCandidateSolve& candidate = result.candidates[i];
        if (candidate.rejected) {
            continue;
        }
        const bool preserveRenderedLocalCandidate =
            renderedFeedbackIterationActive &&
            IsRenderedLocalRefineCandidateId(candidate.id);
        const bool preserveCleanupProbeCandidate =
            IsDevelopCleanupProbeCandidateId(candidate.id);
        const bool preserveModeNeighborCandidate =
            IsDevelopModeNeighborCandidateId(candidate.id);
        const bool preserveFinishToneProbeCandidate =
            IsDevelopFinishToneProbeCandidateId(candidate.id);
        const bool preserveSpecularToleranceCandidate =
            candidate.id == "specularHighlightTolerance";
        const bool preserveNaturalContrastGuardCandidate =
            candidate.id == "naturalContrastGuard";
        const bool preserveLuminousHighlightAnchorCandidate =
            candidate.id == "luminousHighlightAnchor";
        const bool preserveWhiteBalanceProbeCandidate =
            candidate.whiteBalanceProbe;
        const bool preserveBroadHighlightGuardCandidate =
            candidate.id == "broadHighlightGuard";
        const bool preserveLocalRangeGuardCandidate =
            candidate.id == "localRangeGuard";
        const bool preserveHaloSafeLocalRangeCandidate =
            candidate.id == "haloSafeLocalRange";
        const bool preserveShadowReadabilityLiftCandidate =
            candidate.id == "shadowReadabilityLift";
        const bool preserveShadowNoiseFloorCandidate =
            candidate.id == "shadowNoiseFloor";
        const bool preserveSubjectIntentCandidate =
            IsDevelopSubjectIntentCandidateId(candidate.id);
        for (std::size_t j = 0; j < i; ++j) {
            const DevelopAutoCandidateSolve& survivor = result.candidates[j];
            if (survivor.rejected) {
                continue;
            }
            // A rendered-local family comes from measured output mismatch, so keep it
            // available even when it is near a generic parameter family.
            if (preserveRenderedLocalCandidate &&
                !IsRenderedLocalRefineCandidateId(survivor.id)) {
                continue;
            }
            // Cleanup/detail probes intentionally vary hidden RAW cleanup during
            // rendered candidate evaluation, so keep them even when visible
            // guidance is near a generic family.
            if (preserveCleanupProbeCandidate &&
                !IsDevelopCleanupProbeCandidateId(survivor.id)) {
                continue;
            }
            // Mode-neighbor probes describe adjacent intent vectors. Prefer
            // keeping that labeled probe over a generic candidate with a very
            // similar visible guidance delta.
            if (preserveModeNeighborCandidate &&
                !IsDevelopModeNeighborCandidateId(survivor.id)) {
                continue;
            }
            // Finish-tone probes validate downstream shoulder/toe/contrast choices
            // against the same pre-finish image, so do not drop them merely for
            // being near the generic base/scene-prep families.
            if (preserveFinishToneProbeCandidate &&
                !IsDevelopFinishToneProbeCandidateId(survivor.id)) {
                continue;
            }
            // Specular tolerance is a Guide 04 exception path: it can be close
            // to other finish-tone probes, but its image intent is specifically
            // "do not overprotect tiny glints."
            if (preserveSpecularToleranceCandidate &&
                survivor.id != "specularHighlightTolerance") {
                continue;
            }
            // Natural Contrast Guard answers Guide 04 brightness-hierarchy
            // damage, so keep it distinct from generic punchier tone probes.
            if (preserveNaturalContrastGuardCandidate &&
                survivor.id != "naturalContrastGuard") {
                continue;
            }
            // Luminous Highlight Anchor is a finish-tone brightness-feeling
            // probe for broad/protected highlights, so keep it distinct from
            // generic rolloff or contrast candidates.
            if (preserveLuminousHighlightAnchorCandidate &&
                survivor.id != "luminousHighlightAnchor") {
                continue;
            }
            // White-balance probes share most visible guidance with the base
            // solve but change RAW color interpretation, so they need to
            // survive clustering long enough for rendered comparison.
            if (preserveWhiteBalanceProbeCandidate &&
                !survivor.whiteBalanceProbe) {
                continue;
            }
            // Broad Highlight Guard is a scene-prep-local highlight candidate;
            // it can look close to raw/global highlight candidates numerically,
            // but the stage being tested is intentionally different.
            if (preserveBroadHighlightGuardCandidate &&
                survivor.id != "broadHighlightGuard") {
                continue;
            }
            // Local Range Guard is emitted from rendered regional evidence and
            // changes scene-prep/local range behavior even when its visible
            // guidance is close to a generic range/readability candidate.
            if (preserveLocalRangeGuardCandidate &&
                survivor.id != "localRangeGuard") {
                continue;
            }
            // Halo-Safe Local Range is the Guide 04 anti-halo Scene Prep
            // check. Keep it distinct from generic range/readability probes.
            if (preserveHaloSafeLocalRangeCandidate &&
                survivor.id != "haloSafeLocalRange") {
                continue;
            }
            // Shadow Readability Lift is the positive Scene Prep counterpart to
            // Shadow Noise Floor: render-test clean local opening separately
            // from generic midtone/range candidates.
            if (preserveShadowReadabilityLiftCandidate &&
                survivor.id != "shadowReadabilityLift") {
                continue;
            }
            // Shadow Noise Floor is a Guide 04 scene-prep candidate for
            // intentionally holding noisy dark regions down. Its visible
            // guidance can be close to mood/tone candidates, but the stage
            // reason is different enough to render-test separately.
            if (preserveShadowNoiseFloorCandidate &&
                survivor.id != "shadowNoiseFloor") {
                continue;
            }
            // Guide 05 subject-intent probes represent alternate user/scene
            // interpretations, so keep them through generic numeric clustering.
            if (preserveSubjectIntentCandidate &&
                !IsDevelopSubjectIntentCandidateId(survivor.id)) {
                continue;
            }
            if (DevelopAutoCandidateDistance(candidate.guidance, survivor.guidance) < 0.18f) {
                candidate.rejected = true;
                candidate.duplicate = true;
                candidate.rejectReason = "Clustered as a duplicate of " + survivor.label + ".";
                break;
            }
        }
    }
    std::vector<std::size_t> survivorIndices;
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        if (!result.candidates[i].rejected) {
            survivorIndices.push_back(i);
        }
    }
    if (survivorIndices.empty()) {
        result.candidates.front().rejected = false;
        result.candidates.front().rejectReason.clear();
        survivorIndices.push_back(0);
    }

    std::sort(survivorIndices.begin(), survivorIndices.end(), [&](std::size_t a, std::size_t b) {
        return result.candidates[a].score > result.candidates[b].score;
    });

    const DevelopAutoCandidateSolve* selected = &result.candidates[survivorIndices.front()];
    result.authoredGuidance = selected->guidance;
    result.selectedId = selected->id;
    result.selectedLabel = selected->label;
    result.selectedScore = selected->score;
    SetDevelopResultWhiteBalanceProbe(result, *selected);

    if (survivorIndices.size() >= 2) {
        const DevelopAutoCandidateSolve firstCandidate = result.candidates[survivorIndices[0]];
        const DevelopAutoCandidateSolve secondCandidate = result.candidates[survivorIndices[1]];
        const float scoreGap = std::fabs(firstCandidate.score - secondCandidate.score);
        const float distance = DevelopAutoCandidateDistance(firstCandidate.guidance, secondCandidate.guidance);
        if (stats.valid &&
            !firstCandidate.whiteBalanceProbe &&
            !secondCandidate.whiteBalanceProbe &&
            scoreGap <= 0.07f &&
            distance >= 0.24f &&
            firstCandidate.score > 0.48f &&
            secondCandidate.score > 0.46f) {
            const float firstWeight = std::clamp(
                0.50f + (firstCandidate.score - secondCandidate.score) * 1.35f,
                0.42f,
                0.68f);
            DevelopAutoCandidateSolve merged;
            merged.id = "mergedAutoPick";
            merged.label = "Merged Auto Pick";
            merged.reason = "Merged the two strongest compatible solver candidates in authored settings space.";
            merged.guidance = BlendDevelopAutoCandidateGuidance(
                firstCandidate.guidance,
                secondCandidate.guidance,
                firstWeight);
            merged.score = std::min(1.0f, std::max(firstCandidate.score, secondCandidate.score) + 0.025f);
            merged.scoreComponents = BuildDevelopAutoCandidateScoreComponents(
                merged,
                modeGuidance,
                intent,
                stats,
                regionEvidence,
                dynamicRangeStrategy,
                subjectSceneIntent,
                darkness,
                shadowRescueNeed,
                hdrNeed,
                flatSceneNeed,
                underBrightBroadHighlightEv);
            result.candidates.push_back(merged);
            result.authoredGuidance = merged.guidance;
            result.selectedId = merged.id;
            result.selectedLabel = merged.label;
            result.selectedScore = merged.score;
            ClearDevelopResultWhiteBalanceProbe(result);
            result.selectionSource = "mergedParameterScore";
            result.mergeApplied = true;
            result.mergeFirstId = firstCandidate.id;
            result.mergeSecondId = secondCandidate.id;
            result.mergeThirdId.clear();
            result.mergeFirstWeight = firstWeight;
            result.mergeSecondWeight = 1.0f - firstWeight;
            result.mergeThirdWeight = 0.0f;
        }
    }

    const std::uint64_t preliminaryFingerprint = BuildDevelopAutoCandidateFingerprint(result, stats);
    ApplyRenderedCandidateFeedbackToSolve(result, previousToneJson, preliminaryFingerprint);
    result.fingerprint = BuildDevelopAutoCandidateFingerprint(result, stats);
    const std::uint64_t previousFingerprint =
        previousToneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
    const std::string previousSelectedId =
        previousToneJson.value("autoCandidateSelectedId", std::string());
    const int previousPass = previousToneJson.value("autoCandidateConvergencePass", 0);
    result.converged =
        stats.valid &&
        previousFingerprint == result.fingerprint &&
        previousSelectedId == result.selectedId &&
        previousPass > 0;
    result.convergencePass = result.converged
        ? previousPass
        : std::min(previousPass + 1, 6);
    return result;
}

std::string DevelopAutoCandidateStatusForDiagnostics(
    const DevelopAutoCandidateSolve& candidate,
    const DevelopAutoCandidateSolveResult& result) {
    if (candidate.id == result.selectedId) {
        return "selected";
    }
    if (!candidate.rejected) {
        return "survivor";
    }
    if (candidate.rememberedRejection) {
        return "rejectedMemory";
    }
    return candidate.duplicate ? "rejectedDuplicate" : "rejectedDamage";
}

nlohmann::json BuildDevelopAutoCandidateLearningRecord(
    const DevelopAutoCandidateSolveResult& result) {
    nlohmann::json events = nlohmann::json::array();
    int selectedCount = 0;
    int survivorCount = 0;
    int rejectedCount = 0;

    auto appendEvent = [&](nlohmann::json event) {
        constexpr std::size_t kMaxLearningEvents = 24;
        if (events.size() < kMaxLearningEvents) {
            events.push_back(std::move(event));
        }
    };

    for (const DevelopAutoCandidateSolve& candidate : result.candidates) {
        const std::string status = DevelopAutoCandidateStatusForDiagnostics(candidate, result);
        if (status == "selected") {
            ++selectedCount;
        } else if (status == "survivor") {
            ++survivorCount;
        } else {
            ++rejectedCount;
        }

        nlohmann::json event;
        event["type"] =
            status == "selected"
                ? "candidateSelected"
                : (status == "survivor" ? "candidateSurvived" : "candidateRejected");
        event["candidateId"] = candidate.id;
        event["label"] = candidate.label;
        event["status"] = status;
        event["score"] = candidate.score;
        event["reason"] = candidate.reason;
        event["selectionSource"] = result.selectionSource;
        event["guidanceFingerprint"] =
            candidate.guidanceFingerprint != 0
                ? candidate.guidanceFingerprint
                : BuildDevelopAutoCandidateGuidanceFingerprint(candidate.id, candidate.guidance);
        event["autoIntent"] = EditorNodeGraph::DevelopAutoIntentStableString(candidate.guidance.intent);
        event["guidanceVector"] = {
            { "brightnessIntent", candidate.guidance.exposureBias },
            { "dynamicRange", candidate.guidance.dynamicRange },
            { "shadowLift", candidate.guidance.shadowLift },
            { "highlightGuard", candidate.guidance.highlightGuard },
            { "highlightCharacter", candidate.guidance.highlightCharacter },
            { "contrastBias", candidate.guidance.contrastBias },
            { "subjectSceneBias", candidate.guidance.subjectSceneBias },
            { "moodReadabilityBias", candidate.guidance.moodReadabilityBias }
        };
        if (!candidate.rejectReason.empty()) {
            event["rejectReason"] = candidate.rejectReason;
        }
        if (candidate.renderedMemoryRejected) {
            event["renderedMemoryRejected"] = true;
        }
        appendEvent(std::move(event));
    }

    if (result.mergeApplied) {
        appendEvent({
            { "type", "candidateMerged" },
            { "candidateId", result.selectedId },
            { "label", result.selectedLabel },
            { "firstId", result.mergeFirstId },
            { "secondId", result.mergeSecondId },
            { "thirdId", result.mergeThirdId },
            { "firstWeight", result.mergeFirstWeight },
            { "secondWeight", result.mergeSecondWeight },
            { "thirdWeight", result.mergeThirdWeight },
            { "selectionSource", result.selectionSource }
        });
    }

    if (result.renderedFeedbackApplied) {
        appendEvent({
            { "type", "renderedFeedbackApplied" },
            { "action", result.renderedFeedbackAction.empty() ? "applied" : result.renderedFeedbackAction },
            { "candidateId", result.selectedId },
            { "previousSelectedId", result.renderedFeedbackPreviousSelectedId },
            { "bestId", result.renderedFeedbackBestId },
            { "bestScore", result.renderedFeedbackBestScore },
            { "improvement", result.renderedFeedbackImprovement },
            { "revisionStage", result.renderedFeedbackRevisionStage },
            { "revisionReason", result.renderedFeedbackRevisionReason },
            { "sourceFingerprint", result.renderedFeedbackSourceFingerprint },
            { "pass", result.renderedFeedbackPass }
        });
    } else if (!result.renderedFeedbackStopReason.empty()) {
        appendEvent({
            { "type", "renderedFeedbackStopped" },
            { "candidateId", result.selectedId },
            { "stopReason", result.renderedFeedbackStopReason },
            { "converged", result.renderedFeedbackStopIsConverged },
            { "stabilityDistance", result.renderedFeedbackStabilityDistance },
            { "stabilityScoreDelta", result.renderedFeedbackStabilityScoreDelta },
            { "trendHistoryCount", result.renderedFeedbackTrendHistoryCount },
            { "trendSameBestCount", result.renderedFeedbackTrendSameBestCount },
            { "trendScoreSpread", result.renderedFeedbackTrendScoreSpread },
            { "trendNearestDistance", result.renderedFeedbackTrendNearestDistance },
            { "monotonicMetric", result.renderedFeedbackMonotonicMetric },
            { "monotonicPreviousValue", result.renderedFeedbackMonotonicPreviousValue },
            { "monotonicCurrentValue", result.renderedFeedbackMonotonicCurrentValue }
        });
    }

    if (result.converged) {
        appendEvent({
            { "type", "candidateSolveConverged" },
            { "candidateId", result.selectedId },
            { "pass", result.convergencePass },
            { "solveFingerprint", result.fingerprint }
        });
    }

    return {
        { "version", "CandidateOutcomeLearningV1" },
        { "status", "recordedNotApplied" },
        { "recorded", true },
        { "applied", false },
        { "appliedToCurrentImage", false },
        { "appliedToFutureImages", false },
        { "applicationReason", "Outcome learning is recorded for diagnostics and future controls, but no preference learning is applied in this pass." },
        { "currentImageLearning", {
            { "recorded", true },
            { "applied", false },
            { "status", "recordedOnly" }
        } },
        { "futureImageLearning", {
            { "recorded", true },
            { "applied", false },
            { "status", "notApplied" }
        } },
        { "userChoiceLearning", {
            { "recorded", false },
            { "applied", false },
            { "status", "deferredUntilCandidateSelectionUi" }
        } },
        { "solveFingerprint", result.fingerprint },
        { "contextFingerprint", result.candidateContextFingerprint },
        { "selectedId", result.selectedId },
        { "selectedLabel", result.selectedLabel },
        { "selectionSource", result.selectionSource },
        { "selectedScore", result.selectedScore },
        { "selectedEventCount", selectedCount },
        { "survivorEventCount", survivorCount },
        { "rejectedEventCount", rejectedCount },
        { "rejectedMemorySuppressionCount", result.rejectedMemorySuppressionCount },
        { "renderedRejectedMemorySuppressionCount", result.renderedRejectedMemorySuppressionCount },
        { "eventCount", static_cast<int>(events.size()) },
        { "maxEventCount", 24 },
        { "events", std::move(events) }
    };
}

void WriteDevelopAutoCandidateSolveDiagnostics(
    nlohmann::json& toneJson,
    const DevelopAutoCandidateSolveResult& result,
    const EditorNodeGraph::DevelopAutoGuidance& baseGuidance) {
    const std::uint64_t previousRenderedFingerprint =
        toneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0));
    const std::uint64_t previousSolveFingerprint =
        toneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
    const std::string previousRenderMetricsStatus =
        toneJson.value("autoCandidateRenderMetricsStatus", std::string());
    const std::uint64_t previousRenderedFeedbackAppliedFingerprint =
        toneJson.value("autoCandidateRenderedFeedbackAppliedFingerprint", static_cast<std::uint64_t>(0));
    const int previousRenderedFeedbackPass =
        toneJson.value("autoCandidateRenderedFeedbackPass", 0);
    const std::string previousRenderedRevisionStage =
        toneJson.value("autoCandidateRenderedRevisionStage", std::string());
    const std::string previousRenderedRevisionReason =
        toneJson.value("autoCandidateRenderedRevisionReason", std::string());
    nlohmann::json rejectedMemory =
        toneJson.value("autoCandidateRejectedMemory", nlohmann::json::array());
    if (!rejectedMemory.is_array()) {
        rejectedMemory = nlohmann::json::array();
    }
    nlohmann::json candidates = nlohmann::json::array();
    int rejectedCount = 0;
    int survivorCount = 0;
    for (std::size_t candidateIndex = 0; candidateIndex < result.candidates.size(); ++candidateIndex) {
        const DevelopAutoCandidateSolve& candidate = result.candidates[candidateIndex];
        if (candidate.rejected) {
            ++rejectedCount;
        } else {
            ++survivorCount;
        }

        nlohmann::json entry;
        const std::uint64_t guidanceFingerprint =
            candidate.guidanceFingerprint != 0
                ? candidate.guidanceFingerprint
                : BuildDevelopAutoCandidateGuidanceFingerprint(candidate.id, candidate.guidance);
        entry["id"] = candidate.id;
        entry["label"] = candidate.label;
        entry["reason"] = candidate.reason;
        entry["score"] = candidate.score;
        entry["guidanceFingerprint"] = guidanceFingerprint;
        entry["status"] = DevelopAutoCandidateStatusForDiagnostics(candidate, result);
        if (!candidate.rejectReason.empty()) {
            entry["rejectReason"] = candidate.rejectReason;
        }
        if (candidate.renderedMemoryRejected) {
            entry["renderedMemoryRejected"] = true;
        }
        entry["changes"] = {
            { "brightnessIntentDelta", candidate.guidance.exposureBias - baseGuidance.exposureBias },
            { "dynamicRangeDelta", candidate.guidance.dynamicRange - baseGuidance.dynamicRange },
            { "shadowLiftDelta", candidate.guidance.shadowLift - baseGuidance.shadowLift },
            { "highlightGuardDelta", candidate.guidance.highlightGuard - baseGuidance.highlightGuard },
            { "highlightCharacterDelta", candidate.guidance.highlightCharacter - baseGuidance.highlightCharacter },
            { "contrastBiasDelta", candidate.guidance.contrastBias - baseGuidance.contrastBias }
        };
        if (candidate.whiteBalanceProbe) {
            entry["rawOverrides"] = {
                { "whiteBalanceMode", Raw::WhiteBalanceModeName(candidate.whiteBalanceMode) },
                { "stage", "rawWhiteBalance" }
            };
            entry["changes"]["whiteBalanceMode"] =
                Raw::WhiteBalanceModeName(candidate.whiteBalanceMode);
        }
        if (candidate.continuationBiasActive) {
            entry["continuationBiasVersion"] = kDevelopContinuationCandidateBiasVersion;
            entry["continuationBiasBonus"] = candidate.continuationBiasBonus;
            entry["continuationBiasReason"] = candidate.continuationBiasReason;
            entry["continuationBiasStage"] = candidate.continuationBiasStage;
            entry["continuationBiasRefineIntent"] = candidate.continuationBiasRefineIntent;
        }
        if (candidate.continuationExpansionCandidate) {
            entry["continuationExpansionVersion"] =
                kDevelopContinuationCandidateExpansionVersion;
            entry["continuationExpansionReason"] =
                candidate.continuationExpansionReason;
            entry["continuationExpansionStage"] =
                candidate.continuationExpansionStage;
            entry["continuationExpansionRefineIntent"] =
                candidate.continuationExpansionRefineIntent;
        }
        entry["guidance"] = {
            { "autoStrength", candidate.guidance.autoStrength },
            { "brightnessIntent", candidate.guidance.exposureBias },
            { "dynamicRange", candidate.guidance.dynamicRange },
            { "shadowLift", candidate.guidance.shadowLift },
            { "highlightGuard", candidate.guidance.highlightGuard },
            { "highlightCharacter", candidate.guidance.highlightCharacter },
            { "contrastBias", candidate.guidance.contrastBias },
            { "subjectSceneBias", candidate.guidance.subjectSceneBias },
            { "moodReadabilityBias", candidate.guidance.moodReadabilityBias }
        };
        nlohmann::json scoreComponents =
            candidate.scoreComponents.is_object() && !candidate.scoreComponents.empty()
                ? candidate.scoreComponents
                : BuildFallbackDevelopAutoCandidateScoreComponents(candidate, baseGuidance);
        scoreComponents["finalScore"] = candidate.score;
        scoreComponents["status"] = entry["status"];
        const float nearestSurvivorDistance =
            DevelopAutoCandidateNearestSurvivorDistance(result, candidateIndex);
        nlohmann::json dimensions =
            scoreComponents.value("dimensions", nlohmann::json::object());
        dimensions["candidateUniqueness"] =
            SaturateFloat(nearestSurvivorDistance / 0.60f);
        dimensions["renderedContinuationFit"] =
            candidate.continuationBiasActive
                ? SaturateFloat(0.50f + candidate.continuationBiasBonus / 0.24f)
                : dimensions.value("renderedContinuationFit", 0.50f);
        dimensions["renderedContinuationCoverage"] =
            candidate.continuationExpansionCandidate
                ? 1.0f
                : dimensions.value("renderedContinuationCoverage", 0.50f);
        scoreComponents["dimensions"] = std::move(dimensions);
        scoreComponents["renderedContinuationBias"] = {
            { "version", kDevelopContinuationCandidateBiasVersion },
            { "active", candidate.continuationBiasActive },
            { "bonus", candidate.continuationBiasBonus },
            { "reason", candidate.continuationBiasReason },
            { "stageFocus", candidate.continuationBiasStage },
            { "refineIntent", candidate.continuationBiasRefineIntent }
        };
        scoreComponents["renderedContinuationExpansion"] = {
            { "version", kDevelopContinuationCandidateExpansionVersion },
            { "active", candidate.continuationExpansionCandidate },
            { "reason", candidate.continuationExpansionReason },
            { "stageFocus", candidate.continuationExpansionStage },
            { "refineIntent", candidate.continuationExpansionRefineIntent }
        };
        scoreComponents["nearestSurvivorDistance"] = nearestSurvivorDistance;
        entry["scoreComponents"] = std::move(scoreComponents);
        candidates.push_back(std::move(entry));

        if (candidate.rejected && !candidate.rememberedRejection && candidate.id != "base") {
            for (auto it = rejectedMemory.begin(); it != rejectedMemory.end();) {
                if (it->is_object() &&
                    it->value("contextFingerprint", static_cast<std::uint64_t>(0)) == result.candidateContextFingerprint &&
                    it->value("id", std::string()) == candidate.id) {
                    it = rejectedMemory.erase(it);
                } else {
                    ++it;
                }
            }

            nlohmann::json memoryEntry;
            memoryEntry["id"] = candidate.id;
            memoryEntry["label"] = candidate.label;
            memoryEntry["reason"] = candidate.rejectReason;
            memoryEntry["status"] = candidate.duplicate ? "rejectedDuplicate" : "rejectedDamage";
            memoryEntry["contextFingerprint"] = result.candidateContextFingerprint;
            memoryEntry["candidateScore"] = candidate.score;
            rejectedMemory.push_back(std::move(memoryEntry));
        }
    }

    constexpr std::size_t kMaxRejectedMemoryEntries = 16;
    while (rejectedMemory.size() > kMaxRejectedMemoryEntries) {
        rejectedMemory.erase(rejectedMemory.begin());
    }

    toneJson["autoCandidateSolveVersion"] = "ParameterCandidatesV1";
    toneJson["autoCandidateScoreVersion"] = "ParameterScoreComponentsV1";
    toneJson["autoCandidateContinuationBiasVersion"] =
        kDevelopContinuationCandidateBiasVersion;
    toneJson["autoCandidateContinuationBiasActive"] =
        result.continuationBiasActive;
    toneJson["autoCandidateContinuationBiasDecision"] =
        result.continuationBiasDecision;
    toneJson["autoCandidateContinuationBiasReason"] =
        result.continuationBiasReason;
    toneJson["autoCandidateContinuationBiasStage"] =
        result.continuationBiasStage;
    toneJson["autoCandidateContinuationBiasRefineIntent"] =
        result.continuationBiasRefineIntent;
    toneJson["autoCandidateContinuationBiasAppliedCount"] =
        result.continuationBiasAppliedCount;
    toneJson["autoCandidateContinuationExpansionVersion"] =
        kDevelopContinuationCandidateExpansionVersion;
    toneJson["autoCandidateContinuationExpansionEligible"] =
        result.continuationExpansionEligible;
    toneJson["autoCandidateContinuationExpansionActive"] =
        result.continuationExpansionAddedCount > 0;
    toneJson["autoCandidateContinuationExpansionReason"] =
        result.continuationExpansionReason;
    toneJson["autoCandidateContinuationExpansionStage"] =
        result.continuationExpansionStage;
    toneJson["autoCandidateContinuationExpansionRefineIntent"] =
        result.continuationExpansionRefineIntent;
    toneJson["autoCandidateContinuationExpansionAddedCount"] =
        result.continuationExpansionAddedCount;
    const nlohmann::json dynamicRangeStrategy =
        result.dynamicRangeStrategy.is_object()
            ? result.dynamicRangeStrategy
            : nlohmann::json::object();
    toneJson["autoDynamicRangeStrategyVersion"] = kDevelopDynamicRangeStrategyVersion;
    toneJson["autoDynamicRangeStrategy"] = dynamicRangeStrategy;
    toneJson["autoDynamicRangeStrategyId"] =
        dynamicRangeStrategy.value("id", std::string("balancedRange"));
    toneJson["autoDynamicRangeStrategyLabel"] =
        dynamicRangeStrategy.value("label", std::string("Balanced Range"));
    toneJson["autoDynamicRangeStrategyReason"] =
        dynamicRangeStrategy.value("reason", std::string());
    toneJson["autoDynamicRangeHighlightPolicy"] =
        dynamicRangeStrategy.value("highlightPolicy", std::string());
    toneJson["autoDynamicRangeShadowPolicy"] =
        dynamicRangeStrategy.value("shadowPolicy", std::string());
    toneJson["autoDynamicRangeHighlightImportance"] =
        dynamicRangeStrategy.value("highlightImportance", 0.0f);
    toneJson["autoDynamicRangeShadowReadability"] =
        dynamicRangeStrategy.value("shadowReadability", 0.0f);
    toneJson["autoDynamicRangeNoiseConstraint"] =
        dynamicRangeStrategy.value("noiseConstraint", 0.0f);
    toneJson["autoDynamicRangeCompression"] =
        dynamicRangeStrategy.value("rangeCompression", 0.0f);
    toneJson["autoDynamicRangeBrightnessHierarchyRisk"] =
        dynamicRangeStrategy.value("brightnessHierarchyRisk", 0.0f);
    toneJson["autoDynamicRangeMeaningfulHighlightPressure"] =
        dynamicRangeStrategy.value("meaningfulHighlightPressure", 0.0f);
    toneJson["autoDynamicRangeNaturalContrastGuardNeed"] =
        dynamicRangeStrategy.value("naturalContrastGuardNeed", 0.0f);
    toneJson["autoDynamicRangeBrightHighlightRolloffNeed"] =
        dynamicRangeStrategy.value("brightHighlightRolloffNeed", 0.0f);
    toneJson["autoDynamicRangeHighlightBrightnessAnchorNeed"] =
        dynamicRangeStrategy.value("highlightBrightnessAnchorNeed", 0.0f);
    toneJson["autoDynamicRangeBroadHighlightGuardNeed"] =
        dynamicRangeStrategy.value("broadHighlightGuardNeed", 0.0f);
    toneJson["autoDynamicRangeSpecularHighlightToleranceNeed"] =
        dynamicRangeStrategy.value("specularHighlightToleranceNeed", 0.0f);
    toneJson["autoDynamicRangeShadowReadabilityLiftNeed"] =
        dynamicRangeStrategy.value("shadowReadabilityLiftNeed", 0.0f);
    toneJson["autoDynamicRangeShadowNoiseFloorNeed"] =
        dynamicRangeStrategy.value("shadowNoiseFloorNeed", 0.0f);
    toneJson["autoDynamicRangeLocalHighlightHotspotRisk"] =
        dynamicRangeStrategy.value("localHighlightHotspotRisk", 0.0f);
    toneJson["autoDynamicRangeLocalShadowHotspotRisk"] =
        dynamicRangeStrategy.value("localShadowHotspotRisk", 0.0f);
    toneJson["autoDynamicRangeLocalRangeConflict"] =
        dynamicRangeStrategy.value("localRangeConflict", 0.0f);
    toneJson["autoDynamicRangeLocalEvConflict"] =
        dynamicRangeStrategy.value("localEvConflict", 0.0f);
    toneJson["autoDynamicRangeLocalHaloRisk"] =
        dynamicRangeStrategy.value("localHaloRisk", 0.0f);
    toneJson["autoDynamicRangeLocalHaloGuardNeed"] =
        dynamicRangeStrategy.value("localHaloGuardNeed", 0.0f);
    toneJson["autoDynamicRangeFlatGrayRisk"] =
        dynamicRangeStrategy.value("flatGrayRisk", 0.0f);
    toneJson["autoDynamicRangeHighlightGrayRisk"] =
        dynamicRangeStrategy.value("highlightGrayRisk", 0.0f);
    const nlohmann::json strategyMap =
        dynamicRangeStrategy.value("strategyMap", nlohmann::json::object());
    toneJson["autoDynamicRangeStrategyMapVersion"] =
        strategyMap.value("version", std::string(kDevelopDynamicRangeStrategyMapVersion));
    toneJson["autoDynamicRangeStrategyMap"] = strategyMap;
    toneJson["autoDynamicRangeStrategyMapHighlightShadowAxis"] =
        strategyMap.value(
            "highlightShadowAxis",
            dynamicRangeStrategy.value("strategyMapHighlightShadowAxis", 0.0f));
    toneJson["autoDynamicRangeStrategyMapContrastRangeAxis"] =
        strategyMap.value(
            "contrastRangeAxis",
            dynamicRangeStrategy.value("strategyMapContrastRangeAxis", 0.0f));
    toneJson["autoDynamicRangeStrategyMapHighlightPriority"] =
        strategyMap.value(
            "highlightPriority",
            dynamicRangeStrategy.value("strategyMapHighlightPriority", 0.5f));
    toneJson["autoDynamicRangeStrategyMapShadowVisibility"] =
        strategyMap.value(
            "shadowVisibility",
            dynamicRangeStrategy.value("strategyMapShadowVisibility", 0.5f));
    toneJson["autoDynamicRangeStrategyMapNaturalContrast"] =
        strategyMap.value(
            "naturalContrast",
            dynamicRangeStrategy.value("strategyMapNaturalContrast", 0.5f));
    toneJson["autoDynamicRangeStrategyMapVisibleRange"] =
        strategyMap.value(
            "visibleRange",
            dynamicRangeStrategy.value("strategyMapVisibleRange", 0.5f));
    toneJson["autoDynamicRangeStrategyMapReason"] =
        strategyMap.value(
            "reason",
            dynamicRangeStrategy.value("strategyMapReason", std::string()));
    const nlohmann::json localExposureStrategy =
        dynamicRangeStrategy.value("localExposureStrategy", nlohmann::json::object());
    toneJson["autoDynamicRangeLocalExposureStrategyVersion"] =
        localExposureStrategy.value(
            "version",
            dynamicRangeStrategy.value(
                "localExposureStrategyVersion",
                std::string(kDevelopLocalExposureStrategyVersion)));
    toneJson["autoDynamicRangeLocalExposureStrategy"] = localExposureStrategy;
    toneJson["autoDynamicRangeLocalExposureStrategyId"] =
        localExposureStrategy.value(
            "id",
            dynamicRangeStrategy.value("localExposureStrategyId", std::string("balancedLocalPrep")));
    toneJson["autoDynamicRangeLocalExposureStrategyLabel"] =
        localExposureStrategy.value(
            "label",
            dynamicRangeStrategy.value("localExposureStrategyLabel", std::string("Balanced Local Prep")));
    toneJson["autoDynamicRangeLocalExposureStrategyReason"] =
        localExposureStrategy.value(
            "reason",
            dynamicRangeStrategy.value("localExposureStrategyReason", std::string()));
    toneJson["autoDynamicRangeLocalExposureRangeRedistribution"] =
        localExposureStrategy.value(
            "rangeRedistribution",
            dynamicRangeStrategy.value("localExposureRangeRedistribution", 0.0f));
    toneJson["autoDynamicRangeLocalExposureHighlightCompression"] =
        localExposureStrategy.value(
            "highlightCompression",
            dynamicRangeStrategy.value("localExposureHighlightCompression", 0.0f));
    toneJson["autoDynamicRangeLocalExposureShadowOpening"] =
        localExposureStrategy.value(
            "shadowOpening",
            dynamicRangeStrategy.value("localExposureShadowOpening", 0.0f));
    toneJson["autoDynamicRangeLocalExposureNoiseGuard"] =
        localExposureStrategy.value(
            "noiseGuard",
            dynamicRangeStrategy.value("localExposureNoiseGuard", 0.0f));
    toneJson["autoDynamicRangeLocalExposureHaloGuard"] =
        localExposureStrategy.value(
            "haloGuard",
            dynamicRangeStrategy.value("localExposureHaloGuard", 0.0f));
    toneJson["autoDynamicRangeLocalExposureTextureGuard"] =
        localExposureStrategy.value(
            "textureGuard",
            dynamicRangeStrategy.value("localExposureTextureGuard", 0.0f));
    toneJson["autoDynamicRangeLocalExposureShadowEvBudget"] =
        localExposureStrategy.value(
            "shadowEvBudget",
            dynamicRangeStrategy.value("localExposureShadowEvBudget", 0.0f));
    toneJson["autoDynamicRangeLocalExposureHighlightEvBudget"] =
        localExposureStrategy.value(
            "highlightEvBudget",
            dynamicRangeStrategy.value("localExposureHighlightEvBudget", 0.0f));
    toneJson["autoDynamicRangeLocalExposureStrengthTarget"] =
        localExposureStrategy.value(
            "strengthTarget",
            dynamicRangeStrategy.value("localExposureStrengthTarget", 0.5f));
    toneJson["autoDynamicRangeLocalExposureHighlightCrowding"] =
        dynamicRangeStrategy.value("localExposureHighlightCrowding", 0.0f);
    toneJson["autoDynamicRangeLocalExposureShadowCrowding"] =
        dynamicRangeStrategy.value("localExposureShadowCrowding", 0.0f);
    toneJson["autoDynamicRangeLocalExposureHaloStress"] =
        dynamicRangeStrategy.value("localExposureHaloStress", 0.0f);
    toneJson["autoDynamicRangeLocalExposureFlatnessRisk"] =
        dynamicRangeStrategy.value("localExposureFlatnessRisk", 0.0f);
    toneJson["autoDynamicRangeLocalExposureDamageRisk"] =
        dynamicRangeStrategy.value("localExposureDamageRisk", 0.0f);
    toneJson["autoDynamicRangeSmallSpecularClippingAllowed"] =
        dynamicRangeStrategy.value("smallSpecularClippingAllowed", false);
    const nlohmann::json regionEvidence =
        dynamicRangeStrategy.value("regionEvidence", nlohmann::json::object());
    toneJson["autoDynamicRangeRegionEvidenceVersion"] =
        kDevelopDynamicRangeRegionEvidenceVersion;
    toneJson["autoDynamicRangeRegionEvidence"] = regionEvidence;
    toneJson["autoDynamicRangeRegionEvidenceValid"] =
        regionEvidence.value("valid", false);
    toneJson["autoDynamicRangeRegionEvidenceSource"] =
        regionEvidence.value("source", std::string());
    toneJson["autoDynamicRangeRegionEvidenceCandidateId"] =
        regionEvidence.value("candidateId", std::string());
    toneJson["autoDynamicRangeLocalEvSpreadStops"] =
        regionEvidence.value("localEvSpreadStops", 0.0f);
    toneJson["autoDynamicRangeSmallSpecularLikely"] =
        regionEvidence.value("smallSpecularLikely", false);
    toneJson["autoDynamicRangeHighlightBandFraction"] =
        regionEvidence.value("highlightBandFraction", 0.0f);
    toneJson["autoDynamicRangeHighlightMeanLuma"] =
        regionEvidence.value("highlightMeanLuma", 0.0f);
    toneJson["autoDynamicRangeHighlightLowSaturationFraction"] =
        regionEvidence.value("highlightLowSaturationFraction", 0.0f);
    toneJson["autoDynamicRangeHighlightTileCoverage"] =
        regionEvidence.value("highlightTileCoverage", 0.0f);
    toneJson["autoDynamicRangeHighlightStructureScore"] =
        regionEvidence.value("highlightStructureScore", 0.0f);
    const nlohmann::json subjectSceneIntent =
        result.subjectSceneIntent.is_object()
            ? result.subjectSceneIntent
            : nlohmann::json::object();
    toneJson["autoSubjectSceneIntentVersion"] =
        subjectSceneIntent.value("version", std::string(kDevelopSubjectSceneIntentVersion));
    toneJson["autoSubjectSceneIntent"] = subjectSceneIntent;
    toneJson["autoSubjectSceneIntentId"] =
        subjectSceneIntent.value("id", std::string("automaticSceneBalance"));
    toneJson["autoSubjectSceneIntentLabel"] =
        subjectSceneIntent.value("label", std::string("Automatic Scene Balance"));
    toneJson["autoSubjectSceneIntentReason"] =
        subjectSceneIntent.value("reason", std::string());
    const nlohmann::json subjectSolveNotes =
        subjectSceneIntent.value("solveNotes", nlohmann::json::array());
    const bool subjectSolveNotesValid = subjectSolveNotes.is_array();
    std::string primarySubjectSolveNote;
    if (subjectSolveNotesValid && !subjectSolveNotes.empty() &&
        subjectSolveNotes.front().is_object()) {
        primarySubjectSolveNote =
            subjectSolveNotes.front().value("text", std::string());
    }
    toneJson["autoSubjectSceneSolveNotesVersion"] =
        subjectSceneIntent.value(
            "solveNotesVersion",
            std::string(kDevelopSubjectImportanceSolveNotesVersion));
    toneJson["autoSubjectSceneSolveNotes"] =
        subjectSolveNotesValid ? subjectSolveNotes : nlohmann::json::array();
    toneJson["autoSubjectSceneSolveNoteCount"] =
        subjectSolveNotesValid ? static_cast<int>(subjectSolveNotes.size()) : 0;
    toneJson["autoSubjectScenePrimarySolveNote"] = primarySubjectSolveNote;
    toneJson["autoSubjectSceneUserGuidanceStatus"] =
        subjectSceneIntent.value("userGuidanceStatus", std::string("notAvailable"));
    toneJson["autoSubjectSceneUserGuidanceActive"] =
        subjectSceneIntent.value("userGuidanceActive", false);
    toneJson["autoSubjectSceneAutomaticOnly"] =
        subjectSceneIntent.value("automaticOnly", true);
    toneJson["autoSubjectSceneUserSubjectSceneBias"] =
        subjectSceneIntent.value("userSubjectSceneBias", 0.0f);
    toneJson["autoSubjectSceneUserMoodReadabilityBias"] =
        subjectSceneIntent.value("userMoodReadabilityBias", 0.0f);
    toneJson["autoSubjectSceneUserGuidanceStrength"] =
        subjectSceneIntent.value("userGuidanceStrength", 0.0f);
    toneJson["autoSubjectSceneAutomaticConfidence"] =
        subjectSceneIntent.value("automaticConfidence", 0.0f);
    toneJson["autoSubjectSceneCenterPrior"] =
        subjectSceneIntent.value("centerPrior", 0.0f);
    toneJson["autoSubjectSceneReadabilityPressure"] =
        subjectSceneIntent.value("readabilityPressure", 0.0f);
    toneJson["autoSubjectSceneProtectionPressure"] =
        subjectSceneIntent.value("protectionPressure", 0.0f);
    toneJson["autoSubjectSceneMoodPreservationPressure"] =
        subjectSceneIntent.value("moodPreservationPressure", 0.0f);
    toneJson["autoSubjectSceneSubjectPriority"] =
        subjectSceneIntent.value("subjectPriority", 0.5f);
    toneJson["autoSubjectSceneSceneIntegrity"] =
        subjectSceneIntent.value("sceneIntegrity", 0.5f);
    toneJson["autoSubjectSceneImproveReadability"] =
        subjectSceneIntent.value("improveReadability", 0.5f);
    toneJson["autoSubjectScenePreserveMood"] =
        subjectSceneIntent.value("preserveMood", 0.5f);
    toneJson["autoSubjectSceneSubjectSceneAxis"] =
        subjectSceneIntent.value("subjectSceneAxis", 0.0f);
    toneJson["autoSubjectSceneMoodReadabilityAxis"] =
        subjectSceneIntent.value("moodReadabilityAxis", 0.0f);
    toneJson["autoSubjectSceneImportanceRegionCount"] =
        subjectSceneIntent.value("importanceRegionCount", 0);
    toneJson["autoSubjectSceneImportanceStrokeCount"] =
        subjectSceneIntent.value("importanceStrokeCount", 0);
    toneJson["autoSubjectSceneImportanceStrength"] =
        subjectSceneIntent.value("importanceStrength", 0.0f);
    toneJson["autoSubjectSceneImportanceImportant"] =
        subjectSceneIntent.value("importanceImportant", 0.0f);
    toneJson["autoSubjectSceneImportanceReveal"] =
        subjectSceneIntent.value("importanceReveal", 0.0f);
    toneJson["autoSubjectSceneImportanceProtect"] =
        subjectSceneIntent.value("importanceProtect", 0.0f);
    toneJson["autoSubjectSceneImportancePreserveMood"] =
        subjectSceneIntent.value("importancePreserveMood", 0.0f);
    toneJson["autoSubjectSceneImportanceIgnore"] =
        subjectSceneIntent.value("importanceIgnore", 0.0f);
    const nlohmann::json subjectImportanceMap =
        subjectSceneIntent.value("importanceMap", nlohmann::json::object());
    toneJson["autoSubjectSceneImportanceMapVersion"] =
        subjectImportanceMap.value("version", std::string(kDevelopSubjectImportanceMapVersion));
    toneJson["autoSubjectSceneImportanceMap"] = subjectImportanceMap;
    toneJson["autoSubjectSceneImportanceMapStatus"] =
        subjectImportanceMap.value("status", std::string("disabled"));
    toneJson["autoSubjectSceneImportanceMapActive"] =
        subjectImportanceMap.value("active", false);
    toneJson["autoSubjectSceneImportanceMapCoverage"] =
        subjectSceneIntent.value("importanceMapCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapPositiveCoverage"] =
        subjectSceneIntent.value("importanceMapPositiveCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapLowPriorityCoverage"] =
        subjectSceneIntent.value("importanceMapLowPriorityCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapRevealCoverage"] =
        subjectSceneIntent.value("importanceMapRevealCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapProtectCoverage"] =
        subjectSceneIntent.value("importanceMapProtectCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapMoodCoverage"] =
        subjectSceneIntent.value("importanceMapMoodCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapPeak"] =
        subjectSceneIntent.value("importanceMapPeak", 0.0f);
    toneJson["autoSubjectSceneImportanceMapConfidence"] =
        subjectSceneIntent.value("importanceMapConfidence", 0.0f);
    toneJson["autoSubjectSceneImportanceMapCenterBias"] =
        subjectSceneIntent.value("importanceMapCenterBias", 0.0f);
    toneJson["autoSubjectSceneImportanceMapEdgeBias"] =
        subjectSceneIntent.value("importanceMapEdgeBias", 0.0f);
    const nlohmann::json subjectRefinedMap =
        subjectSceneIntent.value("refinedImportanceMap", nlohmann::json::object());
    toneJson["autoSubjectSceneRefinedMapVersion"] =
        subjectRefinedMap.value("version", std::string(kDevelopSubjectRefinedMapVersion));
    toneJson["autoSubjectSceneRefinedMap"] = subjectRefinedMap;
    toneJson["autoSubjectSceneRefinedMapStatus"] =
        subjectRefinedMap.value("status", std::string("disabled"));
    toneJson["autoSubjectSceneRefinedMapActive"] =
        subjectRefinedMap.value("active", false);
    toneJson["autoSubjectSceneRefinedMapCoverage"] =
        subjectSceneIntent.value("refinedMapCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapLowPriorityCoverage"] =
        subjectSceneIntent.value("refinedMapLowPriorityCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapReadabilityCoverage"] =
        subjectSceneIntent.value("refinedMapReadabilityCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapProtectionCoverage"] =
        subjectSceneIntent.value("refinedMapProtectionCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapMoodCoverage"] =
        subjectSceneIntent.value("refinedMapMoodCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapPeak"] =
        subjectSceneIntent.value("refinedMapPeak", 0.0f);
    toneJson["autoSubjectSceneRefinedMapConfidence"] =
        subjectSceneIntent.value("refinedMapConfidence", 0.0f);
    toneJson["autoSubjectSceneRefinedMapBoundaryHint"] =
        subjectSceneIntent.value("refinedMapBoundaryHint", 0.0f);
    toneJson["autoSubjectSceneBrushStatus"] =
        subjectSceneIntent.value("importanceStrokeCount", 0) > 0
            ? "brushStrokesActiveEdgeRefineDeferred"
            : (subjectSceneIntent.value("importanceRegionCount", 0) > 0
                ? "regionGuidanceActive"
                : "deferred");
    toneJson["autoCandidateSolves"] = std::move(candidates);
    toneJson["autoCandidateSurvivorCount"] = survivorCount;
    toneJson["autoCandidateRejectedCount"] = rejectedCount;
    toneJson["autoCandidateContextFingerprint"] = result.candidateContextFingerprint;
    toneJson["autoCandidateRejectedMemory"] = std::move(rejectedMemory);
    toneJson["autoCandidateRejectedMemoryMaxEntries"] = static_cast<int>(kMaxRejectedMemoryEntries);
    toneJson["autoCandidateRejectedMemorySuppressionCount"] =
        result.rejectedMemorySuppressionCount;
    toneJson["autoCandidateRenderedRejectedMemorySuppressionCount"] =
        result.renderedRejectedMemorySuppressionCount;
    toneJson["autoCandidateSelectedId"] = result.selectedId;
    toneJson["autoCandidateSelectedLabel"] = result.selectedLabel;
    toneJson["autoCandidateSelectedScore"] = result.selectedScore;
    toneJson["autoCandidateSelectionSource"] = result.selectionSource;
    toneJson["autoCandidateSelectedWhiteBalanceProbe"] = result.authoredWhiteBalanceProbe;
    toneJson["autoCandidateSelectedWhiteBalanceMode"] =
        result.authoredWhiteBalanceProbe
            ? Raw::WhiteBalanceModeName(result.authoredWhiteBalanceMode)
            : std::string();
    toneJson["autoCandidateMergeApplied"] = result.mergeApplied;
    toneJson["autoCandidateMergeFirstId"] = result.mergeFirstId;
    toneJson["autoCandidateMergeSecondId"] = result.mergeSecondId;
    toneJson["autoCandidateMergeThirdId"] = result.mergeThirdId;
    toneJson["autoCandidateMergeFirstWeight"] = result.mergeFirstWeight;
    toneJson["autoCandidateMergeSecondWeight"] = result.mergeSecondWeight;
    toneJson["autoCandidateMergeThirdWeight"] = result.mergeThirdWeight;
    toneJson["autoCandidateConverged"] = result.converged;
    toneJson["autoCandidateConvergencePass"] = result.convergencePass;
    toneJson["autoCandidateMaxPasses"] = 6;
    toneJson["autoCandidateSolveFingerprint"] = result.fingerprint;
    toneJson["autoCandidateSelectionIsAuthoredState"] = true;
    toneJson["autoCandidateGalleryStatus"] = "deferred";
    const bool renderedMetricsMatchCurrentSolve =
        previousRenderedFingerprint == result.fingerprint &&
        !previousRenderMetricsStatus.empty();
    const bool renderedMetricsStoppedFromPreviousSolve =
        previousRenderedFingerprint != 0 &&
        previousRenderedFingerprint == previousSolveFingerprint &&
        !previousRenderMetricsStatus.empty() &&
        !result.renderedFeedbackApplied &&
        !result.renderedFeedbackStopReason.empty();
    const bool renderedMetricsReadyForCurrentSolve =
        (previousRenderedFingerprint == result.fingerprint || renderedMetricsStoppedFromPreviousSolve) &&
        (previousRenderMetricsStatus == "ready" || previousRenderMetricsStatus == "partial");
    toneJson["autoCandidateRenderMetricsStatus"] =
        (renderedMetricsMatchCurrentSolve || renderedMetricsStoppedFromPreviousSolve)
            ? previousRenderMetricsStatus
            : "pending";
    if (!renderedMetricsMatchCurrentSolve && !renderedMetricsStoppedFromPreviousSolve) {
        toneJson["autoCandidateRenderedConverged"] = false;
        toneJson["autoCandidateRenderedConvergenceStatus"] = "pending";
        toneJson["autoCandidateRenderedStopReason"] = "awaitingRenderedMetrics";
    }
    toneJson["autoCandidateRenderedFeedbackImprovement"] =
        result.renderedFeedbackImprovement;
    toneJson["autoCandidateRenderedStabilityDistance"] =
        result.renderedFeedbackStabilityDistance;
    toneJson["autoCandidateRenderedStabilityScoreDelta"] =
        result.renderedFeedbackStabilityScoreDelta;
    toneJson["autoCandidateRenderedStabilityReferenceId"] =
        result.renderedFeedbackStabilityReferenceId;
    toneJson["autoCandidateRenderedStabilityStatus"] =
        result.renderedFeedbackStabilityDistance >= 0.0f
            ? (result.renderedFeedbackStopReason == "renderedMetricsStable" ? "stable" : "measured")
            : "unavailable";
    toneJson["autoCandidateRenderedTrendHistoryCount"] =
        result.renderedFeedbackTrendHistoryCount;
    toneJson["autoCandidateRenderedTrendSameBestCount"] =
        result.renderedFeedbackTrendSameBestCount;
    toneJson["autoCandidateRenderedTrendScoreSpread"] =
        result.renderedFeedbackTrendScoreSpread;
    toneJson["autoCandidateRenderedTrendNearestDistance"] =
        result.renderedFeedbackTrendNearestDistance;
    toneJson["autoCandidateRenderedTrendReferenceId"] =
        result.renderedFeedbackTrendReferenceId;
    toneJson["autoCandidateRenderedTrendStatus"] =
        result.renderedFeedbackTrendHistoryCount > 0
            ? (result.renderedFeedbackStopReason == "renderedFeedbackNoImprovementTrend" ||
               result.renderedFeedbackStopReason == "renderedRefineNoImprovementTrend" ||
               result.renderedFeedbackStopReason == "renderedFeedbackStableTrend"
                   ? "stalled"
                   : "measured")
            : "unavailable";
    toneJson["autoCandidateRenderedMonotonicGuardStatus"] =
        !result.renderedFeedbackMonotonicMetric.empty()
            ? (result.renderedFeedbackStopReason.rfind("renderedRefineMonotonic", 0) == 0
                ? "stopped"
                : "measured")
            : "unavailable";
    toneJson["autoCandidateRenderedMonotonicMetric"] =
        result.renderedFeedbackMonotonicMetric;
    toneJson["autoCandidateRenderedMonotonicPreviousValue"] =
        result.renderedFeedbackMonotonicPreviousValue;
    toneJson["autoCandidateRenderedMonotonicCurrentValue"] =
        result.renderedFeedbackMonotonicCurrentValue;
    toneJson["autoCandidateRenderedMonotonicReferenceId"] =
        result.renderedFeedbackMonotonicReferenceId;
    toneJson["autoCandidateRenderedCarriedForwardCount"] =
        result.renderedFeedbackCarriedForwardCount;
    toneJson["autoCandidateRenderedFeedbackApplied"] = result.renderedFeedbackApplied;
    toneJson["autoCandidateRenderedFeedbackAppliedFingerprint"] =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackSourceFingerprint
            : previousRenderedFeedbackAppliedFingerprint;
    toneJson["autoCandidateRenderedFeedbackPass"] =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackPass
            : previousRenderedFeedbackPass;
    if (result.renderedFeedbackApplied) {
        toneJson["autoCandidateRenderedFeedbackAction"] =
            result.renderedFeedbackAction.empty() ? "applied" : result.renderedFeedbackAction;
        toneJson["autoCandidateRenderedFeedbackStopReason"] = std::string();
        toneJson["autoCandidateRenderedConvergenceStatus"] =
            result.renderedFeedbackAction.empty() ? "feedbackApplied" : result.renderedFeedbackAction;
        toneJson["autoCandidateRenderedConverged"] = false;
        toneJson["autoCandidateRenderedFeedbackMerged"] =
            result.renderedFeedbackAction == "merged";
        toneJson["autoCandidateRenderedFeedbackPreviousSelectedId"] =
            result.renderedFeedbackPreviousSelectedId;
        toneJson["autoCandidateRenderedFeedbackPreviousSelectedScore"] =
            result.renderedFeedbackPreviousSelectedScore;
        toneJson["autoCandidateRenderedFeedbackBestId"] = result.renderedFeedbackBestId;
        toneJson["autoCandidateRenderedFeedbackBestScore"] = result.renderedFeedbackBestScore;
        if (!result.renderedFeedbackRefineIntent.empty()) {
            toneJson["autoCandidateRenderedFeedbackRefineIntent"] =
                result.renderedFeedbackRefineIntent;
            toneJson["autoCandidateRenderedFeedbackRefineReason"] =
                result.renderedFeedbackRefineReason;
        }
        toneJson["autoCandidateRenderedRevisionStage"] =
            result.renderedFeedbackRevisionStage.empty()
                ? "multiStage"
                : result.renderedFeedbackRevisionStage;
        toneJson["autoCandidateRenderedRevisionReason"] =
            result.renderedFeedbackRevisionReason;
    } else if (renderedMetricsReadyForCurrentSolve && !result.renderedFeedbackStopReason.empty()) {
        toneJson["autoCandidateRenderedFeedbackAction"] = "stopped";
        toneJson["autoCandidateRenderedFeedbackStopReason"] =
            result.renderedFeedbackStopReason;
        toneJson["autoCandidateRenderedConvergenceStatus"] = "stopped";
        toneJson["autoCandidateRenderedStopReason"] =
            result.renderedFeedbackStopReason;
        toneJson["autoCandidateRenderedConverged"] =
            result.renderedFeedbackStopIsConverged;
        toneJson["autoCandidateRenderedRevisionStage"] =
            result.renderedFeedbackStopIsConverged ? "converged" : "none";
        toneJson["autoCandidateRenderedRevisionReason"] =
            result.renderedFeedbackStopReason;
    } else {
        toneJson["autoCandidateRenderedRevisionStage"] =
            previousRenderedRevisionStage;
        toneJson["autoCandidateRenderedRevisionReason"] =
            previousRenderedRevisionReason;
    }

    const nlohmann::json renderedHistory =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    const int renderedHistoryCount =
        renderedHistory.is_array() ? static_cast<int>(renderedHistory.size()) : 0;
    const std::string currentRenderMetricsStatus =
        toneJson.value("autoCandidateRenderMetricsStatus", std::string());
    std::string loopState = "awaitingRenderedMetrics";
    std::string loopAction = "pendingRender";
    std::string loopStopReason =
        toneJson.value("autoCandidateRenderedStopReason", std::string("awaitingRenderedMetrics"));
    std::string loopNextStep = "renderCandidates";
    int loopPass = previousRenderedFeedbackPass;
    int loopNextPass = previousRenderedFeedbackPass;
    bool loopRequiresRenderedMetrics = true;
    bool loopRequiresAutoSolve = false;
    if (result.renderedFeedbackApplied) {
        loopState = "active";
        loopAction = result.renderedFeedbackAction.empty()
            ? std::string("applied")
            : result.renderedFeedbackAction;
        loopStopReason.clear();
        loopNextStep = "renderUpdatedSolve";
        loopPass = result.renderedFeedbackPass;
        loopNextPass = result.renderedFeedbackPass;
        loopRequiresRenderedMetrics = true;
    } else if (renderedMetricsReadyForCurrentSolve && !result.renderedFeedbackStopReason.empty()) {
        loopState = result.renderedFeedbackStopIsConverged ? "converged" : "stopped";
        loopAction = "stopped";
        loopStopReason = result.renderedFeedbackStopReason;
        loopNextStep = "none";
        loopPass = previousRenderedFeedbackPass;
        loopNextPass = previousRenderedFeedbackPass;
        loopRequiresRenderedMetrics = false;
    } else if (!renderedMetricsMatchCurrentSolve) {
        loopState = "awaitingRenderedMetrics";
        loopAction = "pendingRender";
        loopStopReason = "awaitingRenderedMetrics";
        loopNextStep = "renderCandidates";
    } else {
        loopState = "measured";
        loopAction = toneJson.value("autoCandidateRenderedFeedbackAction", std::string());
        loopStopReason = toneJson.value("autoCandidateRenderedStopReason", std::string());
        loopNextStep = "none";
        loopRequiresRenderedMetrics = false;
    }

    const std::string continuationDecision =
        result.renderedFeedbackApplied
            ? std::string("continue")
            : (loopState == "awaitingRenderedMetrics"
                ? std::string("waitForRenderedMetrics")
                : std::string("stop"));
    const std::string continuationReason =
        result.renderedFeedbackApplied
            ? (result.renderedFeedbackAction.empty()
                ? std::string("renderedFeedbackApplied")
                : result.renderedFeedbackAction)
            : loopStopReason;
    const nlohmann::json continuationPolicy =
        BuildDevelopRenderedContinuationPolicyRecord(
            continuationDecision,
            continuationReason,
            loopNextStep,
            loopRequiresAutoSolve,
            loopRequiresRenderedMetrics,
            loopPass,
            loopNextPass,
            toneJson.value("autoCandidateRenderedRevisionStage", std::string()),
            toneJson.value("autoCandidateRenderedRevisionReason", std::string()),
            result.renderedFeedbackImprovement,
            toneJson.value("autoCandidateRenderedStabilityStatus", std::string()),
            toneJson.value("autoCandidateRenderedTrendStatus", std::string()),
            toneJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()));
    toneJson["autoCandidateRenderedContinuationVersion"] =
        kDevelopRenderedContinuationVersion;
    toneJson["autoCandidateRenderedContinuationPolicy"] = continuationPolicy;

    const nlohmann::json convergenceEvidence =
        BuildDevelopAutoConvergenceEvidenceRecord(
            result,
            renderedMetricsMatchCurrentSolve,
            renderedMetricsReadyForCurrentSolve,
            currentRenderMetricsStatus,
            loopState,
            loopAction,
            loopStopReason,
            loopNextStep,
            loopRequiresAutoSolve,
            loopRequiresRenderedMetrics,
            loopPass,
            loopNextPass,
            renderedHistoryCount,
            continuationPolicy,
            toneJson);
    toneJson["autoCandidateConvergenceEvidenceVersion"] =
        kDevelopConvergenceEvidenceVersion;
    toneJson["autoCandidateConvergenceState"] =
        convergenceEvidence.value("state", std::string());
    toneJson["autoCandidateConvergenceDecision"] =
        convergenceEvidence.value("decision", std::string());
    toneJson["autoCandidateConvergenceReason"] =
        convergenceEvidence.value("reason", std::string());
    toneJson["autoCandidateConvergenceShouldContinue"] =
        convergenceEvidence.value("shouldContinue", false);
    toneJson["autoCandidateConvergenceAdmissionVersion"] =
        kDevelopConvergenceAdmissionVersion;
    toneJson["autoCandidateConvergenceAdmissionMinimumImprovement"] =
        result.renderedFeedbackAdmissionMinimumImprovement;
    toneJson["autoCandidateConvergenceAdmissionBaseMinimumImprovement"] =
        result.renderedFeedbackAdmissionBaseMinimumImprovement;
    toneJson["autoCandidateConvergenceAdmissionTightened"] =
        result.renderedFeedbackAdmissionTightened;
    toneJson["autoCandidateConvergenceAdmissionReason"] =
        result.renderedFeedbackAdmissionReason;
    toneJson["autoCandidateConvergenceAdmissionEvidenceState"] =
        result.renderedFeedbackAdmissionEvidenceState;
    toneJson["autoCandidateConvergenceAdmissionEvidenceDecision"] =
        result.renderedFeedbackAdmissionEvidenceDecision;
    toneJson["autoCandidateConvergenceAdmissionEvidencePass"] =
        result.renderedFeedbackAdmissionEvidencePass;
    toneJson["autoCandidateConvergenceEvidence"] = convergenceEvidence;

    toneJson["autoCandidateRenderedFeedbackLoopVersion"] =
        kDevelopRenderedFeedbackLoopVersion;
    toneJson["autoCandidateRenderedFeedbackLoop"] = {
        { "version", kDevelopRenderedFeedbackLoopVersion },
        { "state", loopState },
        { "action", loopAction },
        { "stopReason", loopStopReason },
        { "nextStep", loopNextStep },
        { "requiresRenderedMetrics", loopRequiresRenderedMetrics },
        { "requiresAutoSolve", loopRequiresAutoSolve },
        { "pass", loopPass },
        { "nextPass", loopNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "solveFingerprint", result.fingerprint },
        { "renderedFingerprint", previousRenderedFingerprint },
        { "appliedRenderedFingerprint", result.renderedFeedbackApplied
            ? result.renderedFeedbackSourceFingerprint
            : previousRenderedFeedbackAppliedFingerprint },
        { "renderMetricsStatus", currentRenderMetricsStatus },
        { "selectedId", result.selectedId },
        { "selectedScore", result.selectedScore },
        { "previousSelectedId", result.renderedFeedbackPreviousSelectedId },
        { "previousSelectedScore", result.renderedFeedbackPreviousSelectedScore },
        { "bestId", result.renderedFeedbackApplied
            ? result.renderedFeedbackBestId
            : toneJson.value("autoCandidateRenderedBestId", std::string()) },
        { "bestScore", result.renderedFeedbackApplied
            ? result.renderedFeedbackBestScore
            : toneJson.value("autoCandidateRenderedBestScore", -1.0f) },
        { "improvement", result.renderedFeedbackImprovement },
        { "revisionStage", toneJson.value("autoCandidateRenderedRevisionStage", std::string()) },
        { "revisionReason", toneJson.value("autoCandidateRenderedRevisionReason", std::string()) },
        { "stabilityStatus", toneJson.value("autoCandidateRenderedStabilityStatus", std::string()) },
        { "trendStatus", toneJson.value("autoCandidateRenderedTrendStatus", std::string()) },
        { "monotonicGuardStatus", toneJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()) },
        { "monotonicMetric", toneJson.value("autoCandidateRenderedMonotonicMetric", std::string()) },
        { "historyCount", renderedHistoryCount },
        { "carriedForwardCount", result.renderedFeedbackCarriedForwardCount },
        { "continuationPolicy", continuationPolicy }
    };

    nlohmann::json learningRecord = BuildDevelopAutoCandidateLearningRecord(result);
    toneJson["autoCandidateLearningVersion"] = learningRecord.value("version", std::string());
    toneJson["autoCandidateLearningStatus"] = learningRecord.value("status", std::string());
    toneJson["autoCandidateLearningRecorded"] = learningRecord.value("recorded", false);
    toneJson["autoCandidateLearningApplied"] = learningRecord.value("applied", false);
    toneJson["autoCandidateLearningAppliedToCurrentImage"] =
        learningRecord.value("appliedToCurrentImage", false);
    toneJson["autoCandidateLearningAppliedToFutureImages"] =
        learningRecord.value("appliedToFutureImages", false);
    toneJson["autoCandidateLearningEventCount"] = learningRecord.value("eventCount", 0);
    toneJson["autoCandidateLearningRecord"] = std::move(learningRecord);
}

int ResolveRawDevelopOrientation(const Raw::RawMetadata& metadata, const Raw::RawDevelopSettings& settings) {
    int exifRotationSteps = 0;
    if (metadata.orientation == 3) exifRotationSteps = 2;
    else if (metadata.orientation == 5) exifRotationSteps = 1;
    else if (metadata.orientation == 6) exifRotationSteps = 1;
    else if (metadata.orientation == 8) exifRotationSteps = 3;

    int manualRotationSteps = 0;
    if (settings.rotationDegrees == 90) manualRotationSteps = 3;
    else if (settings.rotationDegrees == 180) manualRotationSteps = 2;
    else if (settings.rotationDegrees == 270) manualRotationSteps = 1;
    const int totalRotationSteps = (exifRotationSteps + manualRotationSteps) % 4;

    int effectiveOrientation = metadata.orientation;
    if (metadata.orientation <= 1 || metadata.orientation == 3 || metadata.orientation == 6 || metadata.orientation == 8) {
        if (totalRotationSteps == 0) effectiveOrientation = 1;
        else if (totalRotationSteps == 1) effectiveOrientation = 6;
        else if (totalRotationSteps == 2) effectiveOrientation = 3;
        else if (totalRotationSteps == 3) effectiveOrientation = 8;
    } else if (metadata.orientation == 2 || metadata.orientation == 4 || metadata.orientation == 5 || metadata.orientation == 7) {
        int baseSteps = 0;
        if (metadata.orientation == 2) baseSteps = 0;
        else if (metadata.orientation == 7) baseSteps = 1;
        else if (metadata.orientation == 4) baseSteps = 2;
        else if (metadata.orientation == 5) baseSteps = 3;

        const int finalSteps = (baseSteps + manualRotationSteps) % 4;
        if (finalSteps == 0) effectiveOrientation = 2;
        else if (finalSteps == 1) effectiveOrientation = 7;
        else if (finalSteps == 2) effectiveOrientation = 4;
        else if (finalSteps == 3) effectiveOrientation = 5;
    }
    return effectiveOrientation;
}

} // namespace

void EditorModule::NormalizeDevelopAutoGuidance(EditorNodeGraph::DevelopAutoGuidance& guidance) {
    guidance.autoStrength = std::clamp(guidance.autoStrength, 0.0f, 2.4f);
    guidance.exposureBias = std::clamp(guidance.exposureBias, -2.0f, 2.0f);
    guidance.dynamicRange = std::clamp(guidance.dynamicRange, 0.25f, 3.0f);
    guidance.shadowLift = std::clamp(guidance.shadowLift, -1.25f, 1.25f);
    guidance.highlightGuard = std::clamp(guidance.highlightGuard, -1.25f, 1.25f);
    guidance.highlightCharacter = std::clamp(guidance.highlightCharacter, -1.25f, 1.25f);
    guidance.contrastBias = std::clamp(guidance.contrastBias, -1.25f, 1.25f);
    guidance.subjectSceneBias = std::clamp(guidance.subjectSceneBias, -1.0f, 1.0f);
    guidance.moodReadabilityBias = std::clamp(guidance.moodReadabilityBias, -1.0f, 1.0f);
}

void EditorModule::NormalizeDevelopSubjectImportance(EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    importance.schemaVersion = std::max(1, importance.schemaVersion);
    importance.overlayOpacity = std::clamp(importance.overlayOpacity, 0.05f, 1.0f);
    importance.interpretedMapOpacity = std::clamp(importance.interpretedMapOpacity, 0.05f, 1.0f);
    importance.refinedMapOpacity = std::clamp(importance.refinedMapOpacity, 0.05f, 1.0f);
    const int brushModeIndex = std::clamp(
        static_cast<int>(importance.brushMode),
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Important),
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore));
    importance.brushMode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(brushModeIndex);
    importance.brushRadius = std::clamp(importance.brushRadius, 0.005f, 0.25f);
    importance.brushFeather = std::clamp(importance.brushFeather, 0.0f, 1.0f);
    importance.brushStrength = std::clamp(importance.brushStrength, 0.0f, 1.0f);
    if (importance.regions.size() > 32) {
        importance.regions.resize(32);
    }
    if (importance.strokes.size() > 128) {
        importance.strokes.erase(
            importance.strokes.begin(),
            importance.strokes.begin() + static_cast<std::ptrdiff_t>(importance.strokes.size() - 128));
    }

    int maxRegionId = 0;
    for (EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (region.id <= 0) {
            region.id = ++maxRegionId;
        }
        maxRegionId = std::max(maxRegionId, region.id);
        const int modeIndex = std::clamp(
            static_cast<int>(region.mode),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Important),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore));
        region.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(modeIndex);
        region.centerX = std::clamp(region.centerX, 0.0f, 1.0f);
        region.centerY = std::clamp(region.centerY, 0.0f, 1.0f);
        region.radiusX = std::clamp(region.radiusX, 0.01f, 1.0f);
        region.radiusY = std::clamp(region.radiusY, 0.01f, 1.0f);
        region.feather = std::clamp(region.feather, 0.0f, 1.0f);
        region.strength = std::clamp(region.strength, 0.0f, 1.0f);
    }
    importance.nextRegionId = std::max(importance.nextRegionId, maxRegionId + 1);
    int maxStrokeId = 0;
    for (EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (stroke.id <= 0) {
            stroke.id = ++maxStrokeId;
        }
        maxStrokeId = std::max(maxStrokeId, stroke.id);
        const int modeIndex = std::clamp(
            static_cast<int>(stroke.mode),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Important),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore));
        stroke.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(modeIndex);
        stroke.radius = std::clamp(stroke.radius, 0.005f, 0.25f);
        stroke.feather = std::clamp(stroke.feather, 0.0f, 1.0f);
        stroke.strength = std::clamp(stroke.strength, 0.0f, 1.0f);
        if (stroke.points.size() > 192) {
            stroke.points.erase(
                stroke.points.begin(),
                stroke.points.begin() + static_cast<std::ptrdiff_t>(stroke.points.size() - 192));
        }
        for (EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            point.x = std::clamp(point.x, 0.0f, 1.0f);
            point.y = std::clamp(point.y, 0.0f, 1.0f);
        }
    }
    importance.nextStrokeId = std::max(importance.nextStrokeId, maxStrokeId + 1);
    if (importance.regions.empty()) {
        importance.activeRegionId = 0;
    } else {
        const auto activeIt = std::find_if(
            importance.regions.begin(),
            importance.regions.end(),
            [&](const EditorNodeGraph::DevelopSubjectImportanceRegion& region) {
                return region.id == importance.activeRegionId;
            });
        if (activeIt == importance.regions.end()) {
            importance.activeRegionId = importance.regions.front().id;
        }
    }
    if (importance.strokes.empty()) {
        importance.activeStrokeId = 0;
    } else {
        const auto activeStrokeIt = std::find_if(
            importance.strokes.begin(),
            importance.strokes.end(),
            [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
                return stroke.id == importance.activeStrokeId;
            });
        if (activeStrokeIt == importance.strokes.end()) {
            importance.activeStrokeId = importance.strokes.back().id;
        }
    }
}

void EditorModule::ApplyDevelopAutoSolve(
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    bool queueToneCalibration,
    bool rewriteRawSettings) {
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    const DevelopAutoIntentProfile intentProfile = ResolveDevelopAutoIntentProfile(payload.autoGuidance.intent);
    const EditorNodeGraph::DevelopAutoGuidance modeGuidance =
        BuildModeAwareDevelopGuidance(payload.autoGuidance, intentProfile);

    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }

    nlohmann::json toneJson = payload.integratedToneLayerJson;
    const DevelopToneAutoStats stats = ReadDevelopToneAutoStats(toneJson);
    const DevelopAutoCandidateSolveResult candidateSolve =
        BuildDevelopAutoCandidateSolve(
            modeGuidance,
            payload.autoGuidance.intent,
            payload.subjectImportance,
            stats,
            metadata,
            toneJson);
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance = candidateSolve.authoredGuidance;
    ApplyDevelopToneGuidanceToJson(toneJson, solveGuidance, payload.autoGuidance.intent);
    toneJson["autoRequestedSceneAssistStrength"] = payload.autoGuidance.autoStrength;
    toneJson["autoRequestedBrightnessIntent"] = payload.autoGuidance.exposureBias;
    toneJson["autoRequestedRawExposurePreferenceEv"] = payload.autoGuidance.exposureBias * 2.0f;
    toneJson["autoRequestedDynamicRange"] = payload.autoGuidance.dynamicRange;
    toneJson["autoRequestedShadowBias"] = payload.autoGuidance.shadowLift;
    toneJson["autoRequestedHighlightBias"] = payload.autoGuidance.highlightGuard;
    toneJson["autoRequestedHighlightCharacter"] = payload.autoGuidance.highlightCharacter;
    toneJson["autoRequestedContrastBias"] = payload.autoGuidance.contrastBias;
    toneJson["autoRequestedSubjectSceneBias"] = payload.autoGuidance.subjectSceneBias;
    toneJson["autoRequestedMoodReadabilityBias"] = payload.autoGuidance.moodReadabilityBias;
    const DevelopSubjectImportanceSummary requestedImportance =
        SummarizeDevelopSubjectImportance(payload.subjectImportance);
    toneJson["autoRequestedSubjectImportanceEnabled"] = payload.subjectImportance.enabled;
    toneJson["autoRequestedSubjectImportanceRegionCount"] =
        requestedImportance.activeRegionCount;
    toneJson["autoRequestedSubjectImportanceStrokeCount"] =
        requestedImportance.activeStrokeCount;
    toneJson["autoRequestedSubjectImportanceStrength"] = requestedImportance.strength;
    const DevelopSubjectImportanceInterpretation requestedImportanceMap =
        InterpretDevelopSubjectImportanceMap(payload.subjectImportance);
    toneJson["autoRequestedSubjectImportanceMapVersion"] =
        kDevelopSubjectImportanceMapVersion;
    toneJson["autoRequestedSubjectImportanceMapStatus"] =
        requestedImportanceMap.status;
    toneJson["autoRequestedSubjectImportanceMapCoverage"] =
        requestedImportanceMap.coverage;
    toneJson["autoRequestedSubjectImportanceMapLowPriorityCoverage"] =
        requestedImportanceMap.lowPriorityCoverage;
    toneJson["autoRequestedSubjectImportanceMapConfidence"] =
        requestedImportanceMap.mapConfidence;
    WriteDevelopAutoCandidateSolveDiagnostics(toneJson, candidateSolve, modeGuidance);

    const Raw::RawDevelopSettings defaults = BuildRawDevelopSettingsFromMetadata(metadata);

    const float autoStrength = solveGuidance.autoStrength;
    const float exposureBias = solveGuidance.exposureBias;
    const float dynamicRange = solveGuidance.dynamicRange;
    const float shadowLift = solveGuidance.shadowLift;
    const float highlightGuard = solveGuidance.highlightGuard;
    const float highlightCharacter = solveGuidance.highlightCharacter;
    const float contrastBias = solveGuidance.contrastBias;
    const DevelopAutoSceneProfile sceneProfile = ResolveDevelopAutoSceneProfile(stats.sceneProfile);
    const nlohmann::json localExposureStrategyCandidate =
        candidateSolve.dynamicRangeStrategy.is_object()
            ? candidateSolve.dynamicRangeStrategy.value(
                "localExposureStrategy",
                nlohmann::json::object())
            : nlohmann::json::object();
    const nlohmann::json localExposureStrategy =
        localExposureStrategyCandidate.is_object()
            ? localExposureStrategyCandidate
            : nlohmann::json::object();
    const float localExposureRangeRedistribution =
        localExposureStrategy.value("rangeRedistribution", 0.0f);
    const float localExposureHighlightCompression =
        localExposureStrategy.value("highlightCompression", 0.0f);
    const float localExposureShadowOpening =
        localExposureStrategy.value("shadowOpening", 0.0f);
    const float localExposureNoiseGuard =
        localExposureStrategy.value("noiseGuard", 0.0f);
    const float localExposureHaloGuard =
        localExposureStrategy.value("haloGuard", 0.0f);
    const float localExposureTextureGuard =
        localExposureStrategy.value("textureGuard", 0.0f);
    const float localExposureShadowEvBudget =
        localExposureStrategy.value("shadowEvBudget", 0.0f);
    const float localExposureHighlightEvBudget =
        localExposureStrategy.value("highlightEvBudget", 0.0f);
    const float localExposureStrengthTarget =
        localExposureStrategy.value("strengthTarget", 0.5f);

    const float darkness =
        stats.valid ? std::clamp((0.18f - stats.midtonePercentile) / 0.18f, 0.0f, 1.0f) : 0.0f;
    const float deepShadow =
        stats.valid ? std::clamp((0.06f - stats.shadowPercentile) / 0.06f, 0.0f, 1.0f) : 0.0f;
    const float sceneKeyMatch = stats.valid
        ? SaturateFloat(
            1.0f -
            std::abs(std::log2(
                std::max(stats.midtonePercentile, 0.0001f) / 0.18f)) / 0.80f)
        : 0.0f;
    const float hdrNeed = SaturateFloat((stats.hdrSpreadEv - 2.8f) / 2.8f);
    const float flatSceneNeed =
        SaturateFloat((2.35f - stats.hdrSpreadEv) / 1.15f) *
        SaturateFloat(1.0f - stats.highlightPressure * 1.35f) *
        SaturateFloat(1.0f - stats.noiseRisk * 0.65f);
    const float shadowRescueNeed = SaturateFloat(darkness * 0.72f + deepShadow * 0.28f);
    const float stableSceneGuard = SaturateFloat(
        sceneKeyMatch *
        (1.0f - shadowRescueNeed * 0.70f) *
        (1.0f - stats.highlightPressure * 0.55f) *
        (1.0f - stats.noiseRisk * 0.40f));
    const float brightWindowNeed = SaturateFloat(
        shadowRescueNeed * (0.48f + stats.highlightPressure * 0.52f) +
        hdrNeed * 0.35f);
    const float highlightNeed = std::clamp(
        stats.highlightPressure +
            std::max(0.0f, highlightGuard) * 0.45f +
            std::max(0.0f, highlightCharacter) * 0.20f,
        0.0f,
        1.50f);
    const float highlightHeavyNeed = SaturateFloat(stats.highlightPressure * 0.70f + hdrNeed * 0.30f);
    const float broadHighlightConstraint = SaturateFloat(
        stats.highlightPressure * 0.62f +
        std::max(0.0f, stats.highlightPercentile - 0.88f) * 2.20f +
        hdrNeed * 0.12f);
    const float tinySpecularAllowance = stats.valid
        ? SaturateFloat((0.010f - stats.clippingRatio) / 0.010f) *
            SaturateFloat((0.72f - stats.highlightPressure) / 0.72f)
        : 0.0f;
    const float rawBaselineLiftNeed = SaturateFloat(
        shadowRescueNeed * 0.72f +
        std::max(0.0f, stats.recommendedBaseEv) * 0.18f);
    float rawExposureBias = intentProfile.rawExposureBias;
    float rawLiftScale = intentProfile.rawLiftScale;
    float rawHighlightRecoveryBias = intentProfile.rawHighlightRecoveryBias;
    float rawNoiseBias = intentProfile.rawNoiseBias;
    float prepStrengthBias = intentProfile.prepStrengthBias;
    float prepShadowBias = intentProfile.prepShadowBias;
    float prepHighlightBias = intentProfile.prepHighlightBias;
    float prepContrastLift = intentProfile.prepContrastLift;
    float prepNoiseBias = intentProfile.prepNoiseBias;
    switch (sceneProfile) {
        case DevelopAutoSceneProfile::HighlightHeavy:
            rawExposureBias += -0.18f;
            rawLiftScale *= 0.88f;
            rawHighlightRecoveryBias += 0.18f;
            prepStrengthBias += 0.04f;
            prepShadowBias += 0.10f;
            prepHighlightBias += 0.20f;
            break;
        case DevelopAutoSceneProfile::ShadowHeavy:
            rawExposureBias += 0.14f;
            rawLiftScale *= 1.10f;
            prepStrengthBias += 0.08f;
            prepShadowBias += 0.18f;
            prepContrastLift += -0.02f;
            break;
        case DevelopAutoSceneProfile::Flat:
            rawLiftScale *= 0.78f;
            prepStrengthBias += -0.08f;
            prepShadowBias += -0.05f;
            prepContrastLift += 0.08f;
            break;
        case DevelopAutoSceneProfile::NoisyLowLight:
            rawExposureBias += 0.06f;
            rawLiftScale *= 0.92f;
            rawNoiseBias += 0.16f;
            prepStrengthBias += -0.04f;
            prepShadowBias += -0.10f;
            prepHighlightBias += 0.08f;
            prepNoiseBias += 0.14f;
            break;
        case DevelopAutoSceneProfile::Balanced:
        default:
            break;
    }
    const float noiseNeed = std::clamp(
        stats.noiseRisk +
            std::max(0.0f, shadowLift) * 0.20f +
            std::max(0.0f, dynamicRange - 1.0f) * 0.12f +
            rawNoiseBias +
            brightWindowNeed * 0.05f,
        0.0f,
        1.0f);
    const float darkNoisyLowLightDenoiseNeed =
        sceneProfile == DevelopAutoSceneProfile::NoisyLowLight
            ? SaturateFloat((0.12f - stats.midtonePercentile) / 0.12f) *
                SaturateFloat((0.42f - stats.highlightPercentile) / 0.42f) *
                noiseNeed
            : 0.0f;
    const float shadowBoost =
        darkness * (0.85f + autoStrength * 0.90f) +
        deepShadow * (0.20f + std::max(0.0f, dynamicRange - 1.0f) * 0.30f) +
        std::max(0.0f, shadowLift) * 0.75f;
    const float recommendedLift =
        std::max(0.0f, stats.recommendedBaseEv) * (0.55f + autoStrength * 0.55f);
    const float recommendedTrim =
        std::min(0.0f, stats.recommendedBaseEv) * (0.18f + autoStrength * 0.18f);
    const float userBiasEv = exposureBias * 2.0f;
    const float shadowBoostScale = std::clamp(
        rawLiftScale *
            (0.88f + shadowRescueNeed * 0.16f + brightWindowNeed * 0.08f) *
            (1.0f - stableSceneGuard * 0.18f) *
            (1.0f - stats.noiseRisk * 0.10f),
        0.55f,
        1.25f);
    const float recommendedLiftScale = std::clamp(
        rawLiftScale *
            (1.0f - stableSceneGuard * 0.22f - flatSceneNeed * 0.12f),
        0.55f,
        1.20f);
    const float broadHighlightExposureReserve =
        broadHighlightConstraint *
        SaturateFloat(stats.highlightPressure) *
        (0.78f + std::max(0.0f, userBiasEv) * 0.25f + brightWindowNeed * 0.12f) *
        (1.0f - tinySpecularAllowance * 0.65f);
    const float highlightExposurePenalty =
        std::max(0.0f, highlightNeed - 0.50f) *
        (0.22f + broadHighlightConstraint * 0.30f + brightWindowNeed * 0.06f) *
        (1.0f - tinySpecularAllowance * 0.45f) +
        broadHighlightExposureReserve;
    const float rawLiftHeadroomScale = std::clamp(
        1.0f - broadHighlightConstraint * 0.22f + tinySpecularAllowance * 0.10f,
        0.68f,
        1.08f);
    const float underBrightBroadHighlightEv = stats.valid && stats.midtonePercentile > 0.08f
        ? std::clamp(std::log2(0.70f / std::max(stats.highlightPercentile, 0.0001f)), 0.0f, 1.25f)
        : 0.0f;
    const float broadHighlightPlacementLift =
        underBrightBroadHighlightEv *
        SaturateFloat((0.58f - stats.highlightPressure) / 0.58f) *
        SaturateFloat((0.012f - stats.clippingRatio) / 0.012f) *
        (1.0f - stats.noiseRisk * 0.35f) *
        (0.52f + autoStrength * 0.26f);

    const float highlightModeScore = std::clamp(
        highlightNeed +
            std::max(0.0f, dynamicRange - 1.0f) * 0.20f +
            hdrNeed * 0.18f +
            brightWindowNeed * 0.08f +
            rawHighlightRecoveryBias,
        0.0f,
        1.50f);
    if (rewriteRawSettings) {
        Raw::RawDevelopSettings authoredSettings = payload.settings;
        authoredSettings.debugView = Raw::RawDebugView::FinalOutput;
        authoredSettings.cameraTransformEnabled = true;
        authoredSettings.debugBypassCameraTransform = false;
        authoredSettings.debugTransposeCameraMatrix = false;
        authoredSettings.cameraTransformSource = defaults.cameraTransformSource;

        float authoredExposure =
            defaults.exposureStops +
            userBiasEv +
            rawExposureBias +
            shadowBoost * shadowBoostScale * rawLiftHeadroomScale +
            recommendedLift * recommendedLiftScale * rawLiftHeadroomScale +
            broadHighlightPlacementLift +
            recommendedTrim -
            highlightExposurePenalty;
        if (stableSceneGuard > 0.0001f) {
            const float stableAnchor = defaults.exposureStops + userBiasEv + rawExposureBias * 0.25f;
            const float guardBlend = 0.22f * stableSceneGuard;
            authoredExposure = authoredExposure * (1.0f - guardBlend) + stableAnchor * guardBlend;
        }
        if (stats.valid &&
            stats.midtonePercentile > 0.14f &&
            stats.highlightPressure < 0.55f &&
            authoredExposure < defaults.exposureStops + userBiasEv - 0.12f) {
            authoredExposure = defaults.exposureStops + userBiasEv - 0.12f;
        }
        authoredSettings.exposureStops = std::clamp(authoredExposure, -8.0f, 8.0f);

        authoredSettings.whiteBalanceMode =
            HasMeaningfulRawWhiteBalanceMetadata(metadata)
                ? Raw::WhiteBalanceMode::AsShot
                : Raw::WhiteBalanceMode::Auto;
        authoredSettings.manualWhiteBalance = defaults.manualWhiteBalance;
        if (candidateSolve.authoredWhiteBalanceProbe) {
            authoredSettings.whiteBalanceMode = candidateSolve.authoredWhiteBalanceMode;
            authoredSettings.manualWhiteBalance = defaults.manualWhiteBalance;
        }

        if (highlightModeScore > 0.80f) {
            authoredSettings.highlightMode = Raw::HighlightReconstructionMode::ColorReconstruction;
        } else if (highlightModeScore > 0.45f) {
            authoredSettings.highlightMode = Raw::HighlightReconstructionMode::Luminance;
        } else if (highlightModeScore > 0.18f) {
            authoredSettings.highlightMode = Raw::HighlightReconstructionMode::ClipNeutral;
        } else {
            authoredSettings.highlightMode = Raw::HighlightReconstructionMode::Off;
        }
        authoredSettings.highlightStrength = std::clamp(0.35f + highlightModeScore * 0.42f, 0.15f, 1.0f);
        authoredSettings.highlightThreshold = std::clamp(
            0.985f - highlightModeScore * 0.08f - std::max(0.0f, highlightGuard) * 0.03f,
            0.82f,
            0.995f);

        authoredSettings.falseColorSuppression = std::clamp(
            0.18f + noiseNeed * 0.25f + highlightModeScore * 0.08f + darkNoisyLowLightDenoiseNeed * 0.14f,
            0.0f,
            1.0f);
        authoredSettings.defringeStrength = std::clamp(0.28f + highlightModeScore * 0.22f, 0.0f, 1.0f);
        authoredSettings.highlightEdgeCleanup = std::clamp(
            0.35f + highlightModeScore * 0.28f + std::max(0.0f, highlightGuard) * 0.10f,
            0.0f,
            1.0f);
        authoredSettings.preserveRealColor = std::clamp(
            0.68f +
                std::max(0.0f, highlightGuard) * 0.10f +
                highlightHeavyNeed * 0.06f -
                std::max(0.0f, highlightCharacter) * 0.08f -
                darkNoisyLowLightDenoiseNeed * 0.05f,
            0.0f,
            1.0f);
        authoredSettings.chromaRadius = highlightModeScore > 0.75f ? 2 : 1;
        authoredSettings.mosaicDenoise.enabled =
            noiseNeed > 0.18f ||
            shadowBoost > 0.55f ||
            sceneProfile == DevelopAutoSceneProfile::NoisyLowLight;
        authoredSettings.mosaicDenoise.hotPixelSuppression = noiseNeed > 0.25f;
        authoredSettings.mosaicDenoise.hotPixelThreshold = std::clamp(
            0.12f - noiseNeed * 0.03f - darkNoisyLowLightDenoiseNeed * 0.02f,
            0.05f,
            0.18f);
        authoredSettings.mosaicDenoise.lumaStrength = std::clamp(
            0.30f + noiseNeed * 0.38f + std::max(0.0f, shadowLift) * 0.08f +
                prepNoiseBias * 0.10f + darkNoisyLowLightDenoiseNeed * 0.24f,
            0.0f,
            1.0f);
        authoredSettings.mosaicDenoise.chromaStrength = std::clamp(
            0.48f + noiseNeed * 0.30f + highlightModeScore * 0.10f +
                prepNoiseBias * 0.06f + darkNoisyLowLightDenoiseNeed * 0.26f,
            0.0f,
            1.0f);
        authoredSettings.mosaicDenoise.radius =
            darkNoisyLowLightDenoiseNeed > 0.45f
                ? 4
                : ((noiseNeed > 0.65f || sceneProfile == DevelopAutoSceneProfile::NoisyLowLight) ? 3 : 2);
        authoredSettings.mosaicDenoise.edgeProtection = std::clamp(
            0.55f + highlightModeScore * 0.18f - std::max(0.0f, shadowLift) * 0.06f -
                darkNoisyLowLightDenoiseNeed * 0.08f,
            0.0f,
            1.0f);
        authoredSettings.mosaicDenoise.iterations =
            (noiseNeed > 0.70f || (sceneProfile == DevelopAutoSceneProfile::NoisyLowLight && noiseNeed > 0.58f)) ? 2 : 1;
        ClampRawMosaicDenoiseSettings(authoredSettings.mosaicDenoise);
        payload.settings = authoredSettings;
    }

    Raw::RawDetailFusionSettings prepSettings = payload.scenePrepSettings;
    prepSettings.autoSafetyEnabled = true;
    prepSettings.overrideMinEv = false;
    prepSettings.overrideMaxEv = false;
    prepSettings.overrideBaseEv = false;
    prepSettings.overrideNoiseProtection = false;
    prepSettings.overrideHighlightProtection = false;
    prepSettings.overrideShadowLiftLimit = false;
    prepSettings.overrideWellExposedTarget = false;
    prepSettings.strength = std::clamp(
        0.50f +
            autoStrength * 0.30f +
            std::max(0.0f, dynamicRange - 1.0f) * 0.14f +
            prepStrengthBias +
            rawBaselineLiftNeed * 0.05f +
            brightWindowNeed * 0.07f -
            stableSceneGuard * 0.10f,
        0.0f,
        1.25f);
    prepSettings.maxEvBias = std::clamp(
        shadowLift * 1.20f +
            shadowBoost * 0.62f +
            std::max(0.0f, dynamicRange - 1.0f) * 0.55f +
            prepShadowBias +
            brightWindowNeed * 0.36f -
            stats.noiseRisk * 0.10f -
            flatSceneNeed * 0.08f,
        -2.0f,
        2.0f);
    prepSettings.minEvBias = std::clamp(
        -std::max(0.0f, dynamicRange - 1.0f) * 0.45f -
            std::max(0.0f, highlightGuard) * 0.35f -
            highlightNeed * 0.18f -
            hdrNeed * 0.22f -
            prepHighlightBias * 0.18f,
        -2.0f,
        2.0f);
    prepSettings.baseEvBias = std::clamp(
        exposureBias * 0.55f +
            stats.recommendedBaseEv * 0.18f +
            rawExposureBias * 0.18f -
            stableSceneGuard * 0.04f,
        -1.25f,
        1.25f);
    prepSettings.noiseProtectionBias = std::clamp(
        noiseNeed * 0.68f +
            std::max(0.0f, shadowLift) * 0.25f +
            prepNoiseBias +
            brightWindowNeed * 0.06f,
        -1.0f,
        1.0f);
    prepSettings.highlightProtectionBias = std::clamp(
        highlightGuard * 0.85f +
            stats.highlightPressure * 0.35f +
            prepHighlightBias +
            hdrNeed * 0.14f +
            brightWindowNeed * 0.10f,
        -1.0f,
        1.0f);
    prepSettings.shadowLiftLimitBias = std::clamp(
        -std::max(0.0f, dynamicRange - 1.0f) * 0.30f -
            std::max(0.0f, shadowLift) * 0.15f +
            (1.0f - noiseNeed) * 0.05f -
            prepShadowBias * 0.20f +
            shadowRescueNeed * 0.06f -
            prepNoiseBias * 0.08f,
        -1.0f,
        1.0f);
    prepSettings.wellExposedTargetBias = std::clamp(
        contrastBias * 0.18f +
            prepContrastLift +
            flatSceneNeed * 0.08f +
            (darkness > 0.35f ? (-0.04f + brightWindowNeed * 0.06f) : 0.04f) +
            brightWindowNeed * 0.07f +
            shadowRescueNeed * 0.03f -
            stableSceneGuard * 0.03f,
        -1.0f,
        1.0f);
    prepSettings.noiseProtection = std::clamp(
        0.58f + noiseNeed * 0.22f + prepNoiseBias * 0.10f,
        0.0f,
        1.0f);
    prepSettings.highlightProtection = std::clamp(
        0.82f +
            highlightNeed * 0.14f +
            std::max(0.0f, highlightGuard) * 0.08f +
            hdrNeed * 0.06f +
            brightWindowNeed * 0.05f,
        0.0f,
        1.0f);
    prepSettings.shadowLiftLimit = std::clamp(
        0.66f -
            std::max(0.0f, shadowLift) * 0.10f -
            std::max(0.0f, dynamicRange - 1.0f) * 0.05f +
            noiseNeed * 0.12f -
            shadowRescueNeed * 0.04f +
            flatSceneNeed * 0.03f,
        0.0f,
        1.0f);
    prepSettings.detailWeight = std::clamp(
        0.50f +
            stats.textureConfidence * 0.20f +
            contrastBias * 0.08f +
            flatSceneNeed * 0.06f -
            prepNoiseBias * 0.04f,
        0.0f,
        1.0f);
    prepSettings.wellExposedTarget = std::clamp(
        0.28f +
            contrastBias * 0.03f +
            std::max(0.0f, exposureBias) * 0.02f +
            flatSceneNeed * 0.03f -
            shadowRescueNeed * 0.015f +
            brightWindowNeed * 0.035f -
            stableSceneGuard * 0.02f,
        0.10f,
        0.55f);
    prepSettings.smoothGradientProtection = std::clamp(0.82f + highlightNeed * 0.10f, 0.0f, 1.0f);
    prepSettings.textureSensitivity = std::clamp(
        0.42f +
            stats.textureConfidence * 0.25f +
            flatSceneNeed * 0.06f -
            prepNoiseBias * 0.06f,
        0.0f,
        1.0f);
    prepSettings.skyBias = std::clamp(
        0.52f +
            std::max(0.0f, highlightGuard) * 0.16f +
            std::max(0.0f, highlightCharacter) * 0.05f +
            hdrNeed * 0.06f,
        0.0f,
        1.0f);
    prepSettings.sampleCount = noiseNeed > 0.60f ? 19 : 17;
    prepSettings.baseRadiusPercent = std::clamp(
        0.010f +
            std::max(0.0f, dynamicRange - 1.0f) * 0.003f +
            flatSceneNeed * 0.0015f +
            brightWindowNeed * 0.0010f,
        0.002f,
        0.030f);
    prepSettings.smoothnessRadius = noiseNeed > 0.65f ? 6 : 5;
    prepSettings.smoothAreaRadius = noiseNeed > 0.65f ? 14 : 12;
    prepSettings.edgeAwareness = std::clamp(
        0.62f +
            stats.textureConfidence * 0.16f +
            brightWindowNeed * 0.06f -
            prepNoiseBias * 0.04f,
        0.0f,
        1.0f);
    prepSettings.haloGuard = std::clamp(
        0.88f + highlightNeed * 0.08f + brightWindowNeed * 0.08f + hdrNeed * 0.05f,
        0.0f,
        1.0f);
    prepSettings.maskDebandDither = (noiseNeed > 0.55f || flatSceneNeed > 0.70f) ? 0.10f : 0.0f;
    // The local exposure strategy turns Guide 04's highlight/shadow/range map
    // into authored Scene Prep pressure. These are coordinated guardrail moves,
    // not one-slider-one-setting edits: bright regions, shadows, noise, halos,
    // and texture have to move together to avoid fake HDR or gray noisy darks.
    prepSettings.strength = std::clamp(
        prepSettings.strength * 0.86f +
            localExposureStrengthTarget * 0.14f +
            localExposureRangeRedistribution * 0.04f,
        0.0f,
        1.25f);
    prepSettings.maxEvBias = std::clamp(
        prepSettings.maxEvBias +
            localExposureShadowOpening * 0.18f +
            localExposureRangeRedistribution * 0.10f +
            localExposureShadowEvBudget * 0.04f -
            localExposureNoiseGuard * 0.10f -
            localExposureHaloGuard * 0.06f,
        -2.0f,
        2.0f);
    prepSettings.minEvBias = std::clamp(
        prepSettings.minEvBias -
            localExposureHighlightCompression * 0.16f -
            localExposureRangeRedistribution * 0.06f -
            localExposureHighlightEvBudget * 0.04f +
            localExposureHaloGuard * 0.03f,
        -2.0f,
        2.0f);
    prepSettings.highlightProtectionBias = std::clamp(
        prepSettings.highlightProtectionBias +
            localExposureHighlightCompression * 0.10f,
        -1.0f,
        1.0f);
    prepSettings.noiseProtectionBias = std::clamp(
        prepSettings.noiseProtectionBias +
            localExposureNoiseGuard * 0.10f,
        -1.0f,
        1.0f);
    prepSettings.shadowLiftLimitBias = std::clamp(
        prepSettings.shadowLiftLimitBias +
            localExposureNoiseGuard * 0.06f +
            localExposureHaloGuard * 0.04f -
            localExposureShadowOpening * 0.04f,
        -1.0f,
        1.0f);
    prepSettings.haloGuard = std::clamp(
        prepSettings.haloGuard +
            localExposureHaloGuard * 0.06f,
        0.0f,
        1.0f);
    prepSettings.smoothGradientProtection = std::clamp(
        prepSettings.smoothGradientProtection +
            localExposureHaloGuard * 0.05f,
        0.0f,
        1.0f);
    prepSettings.edgeAwareness = std::clamp(
        prepSettings.edgeAwareness +
            localExposureHaloGuard * 0.04f +
            localExposureTextureGuard * 0.03f,
        0.0f,
        1.0f);
    prepSettings.textureSensitivity = std::clamp(
        prepSettings.textureSensitivity +
            localExposureTextureGuard * 0.04f -
            localExposureNoiseGuard * 0.02f,
        0.0f,
        1.0f);
    ClampIntegratedDevelopScenePrepSettings(prepSettings);
    payload.scenePrepSettings = prepSettings;

    toneJson["autoExposureDiagnosticStatsValid"] = stats.valid;
    toneJson["autoAuthoredRawExposureEv"] = payload.settings.exposureStops;
    toneJson["autoAuthoredRawExposureScale"] = std::exp2(payload.settings.exposureStops);
    toneJson["autoAuthoredWhiteBalanceProbe"] = candidateSolve.authoredWhiteBalanceProbe;
    toneJson["autoAuthoredWhiteBalanceMode"] = Raw::WhiteBalanceModeName(payload.settings.whiteBalanceMode);
    toneJson["autoAuthoredLocalMinEvBias"] = prepSettings.minEvBias;
    toneJson["autoAuthoredLocalMaxEvBias"] = prepSettings.maxEvBias;
    toneJson["autoAuthoredLocalExposureStrategyVersion"] =
        localExposureStrategy.value("version", std::string(kDevelopLocalExposureStrategyVersion));
    toneJson["autoAuthoredLocalExposureStrategyId"] =
        localExposureStrategy.value("id", std::string("balancedLocalPrep"));
    toneJson["autoAuthoredLocalExposureStrategyLabel"] =
        localExposureStrategy.value("label", std::string("Balanced Local Prep"));
    toneJson["autoAuthoredLocalExposureRangeRedistribution"] =
        localExposureRangeRedistribution;
    toneJson["autoAuthoredLocalExposureHighlightCompression"] =
        localExposureHighlightCompression;
    toneJson["autoAuthoredLocalExposureShadowOpening"] =
        localExposureShadowOpening;
    toneJson["autoAuthoredLocalExposureNoiseGuard"] =
        localExposureNoiseGuard;
    toneJson["autoAuthoredLocalExposureHaloGuard"] =
        localExposureHaloGuard;
    toneJson["autoAuthoredLocalExposureTextureGuard"] =
        localExposureTextureGuard;
    toneJson["autoAuthoredLocalExposureShadowEvBudget"] =
        localExposureShadowEvBudget;
    toneJson["autoAuthoredLocalExposureHighlightEvBudget"] =
        localExposureHighlightEvBudget;
    toneJson["autoAuthoredLocalExposureStrengthTarget"] =
        localExposureStrengthTarget;
    toneJson["autoExposureDiagnosticClippingRatio"] = stats.clippingRatio;
    toneJson["autoExposureDiagnosticHighlightPressure"] = stats.highlightPressure;
    toneJson["autoExposureDiagnosticNoiseRisk"] = stats.noiseRisk;
    toneJson["autoExposureDiagnosticHdrSpreadEv"] = stats.hdrSpreadEv;
    toneJson["autoExposureDiagnosticRecommendedBaseEv"] = stats.recommendedBaseEv;
    WriteDevelopAutoStageSolveDiagnostics(
        toneJson,
        metadata,
        payload.settings,
        prepSettings,
        solveGuidance,
        candidateSolve,
        stats);

    if (queueToneCalibration) {
        QueueDevelopToneCalibration(toneJson);
    }
    payload.integratedToneLayerJson = std::move(toneJson);
}

namespace {

std::shared_ptr<LayerBase> CloneLayerInstance(const std::shared_ptr<LayerBase>& source) {
    if (!source) {
        return nullptr;
    }
    const nlohmann::json layerJson = source->Serialize();
    const std::string typeId = layerJson.value("type", std::string());
    std::shared_ptr<LayerBase> clone = LayerRegistry::CreateLayerFromTypeId(typeId);
    if (!clone) {
        return nullptr;
    }
    clone->InitializeGL();
    clone->Deserialize(layerJson);
    clone->SetVisible(source->IsVisible());
    return clone;
}

struct GraphReconnectPlan {
    int fromNodeId = 0;
    std::string fromSocketId;
    int toNodeId = 0;
    std::string toSocketId;
};

struct ScenePathState {
    bool sceneReferred = false;
    bool hasViewTransform = false;
};

const EditorNodeGraph::Node* FindUpstreamRawSourceNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& rawDomainNode) {
    const EditorNodeGraph::Link* rawInput = graph.FindInputLink(rawDomainNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

ScenePathState MergeScenePathState(ScenePathState a, const ScenePathState& b) {
    a.sceneReferred = a.sceneReferred || b.sceneReferred;
    a.hasViewTransform = a.hasViewTransform || b.hasViewTransform;
    return a;
}

ScenePathState AnalyzeScenePathFromNode(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers,
    int nodeId,
    std::unordered_set<int>& visiting) {
    if (!visiting.insert(nodeId).second) {
        return {};
    }

    ScenePathState state;
    const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
    if (!node) {
        visiting.erase(nodeId);
        return state;
    }

    auto mergeInput = [&](const char* socketId) {
        if (const EditorNodeGraph::Link* input = graph.FindInputLink(nodeId, socketId)) {
            state = MergeScenePathState(state, AnalyzeScenePathFromNode(graph, layers, input->fromNodeId, visiting));
        }
    };

    switch (node->kind) {
        case EditorNodeGraph::NodeKind::RawDevelop:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kRawInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::HdrMerge:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kHdrMergeInput1SocketId);
            mergeInput(EditorNodeGraph::kHdrMergeInput2SocketId);
            mergeInput(EditorNodeGraph::kHdrMergeInput3SocketId);
            break;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            mergeInput(EditorNodeGraph::kRawInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            if (node->layerType == LayerType::ToneCurve) {
                state.sceneReferred = true;
            } else if (node->layerType == LayerType::ViewTransform) {
                state.hasViewTransform = true;
            }
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Mix:
            mergeInput(EditorNodeGraph::kMixInputASocketId);
            mergeInput(EditorNodeGraph::kMixInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            mergeInput(EditorNodeGraph::kMixInputASocketId);
            mergeInput(EditorNodeGraph::kMixInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            mergeInput("r");
            mergeInput("g");
            mergeInput("b");
            mergeInput("a");
            break;
        case EditorNodeGraph::NodeKind::Output:
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            mergeInput(EditorNodeGraph::kImageToMaskInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            mergeInput(EditorNodeGraph::kMaskCombineInputASocketId);
            mergeInput(EditorNodeGraph::kMaskCombineInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            mergeInput(EditorNodeGraph::kMaskUtilityInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::Preview:
            break;
    }

    visiting.erase(nodeId);
    (void)layers;
    return state;
}

std::optional<GraphReconnectPlan> BuildReconnectSourcePlan(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer: {
            const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::MaskUtility: {
            const EditorNodeGraph::Link* input = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kMaskOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::MaskCombine: {
            const EditorNodeGraph::Link* inputA = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskCombineInputASocketId);
            const EditorNodeGraph::Link* inputB = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskCombineInputBSocketId);
            const EditorNodeGraph::Link* selectedInput =
                inputA && !inputB ? inputA :
                inputB && !inputA ? inputB :
                nullptr;
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kMaskOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: {
            const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kRawOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::Mix: {
            const EditorNodeGraph::Link* inputA = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputASocketId);
            const EditorNodeGraph::Link* inputB = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputBSocketId);
            const EditorNodeGraph::Link* selectedInput =
                inputA && !inputB ? inputA :
                inputB && !inputA ? inputB :
                nullptr;
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::DataMath: {
            const EditorNodeGraph::Link* inputA = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputASocketId);
            const EditorNodeGraph::Link* inputB = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputBSocketId);
            const EditorNodeGraph::Link* selectedInput =
                inputA && !inputB ? inputA :
                inputB && !inputA ? inputB :
                nullptr;
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        default:
            break;
    }
    return std::nullopt;
}

std::vector<GraphReconnectPlan> BuildReconnectPlansForNodeRemoval(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const std::optional<GraphReconnectPlan> sourcePlan = BuildReconnectSourcePlan(graph, node);
    if (!sourcePlan.has_value()) {
        return {};
    }

    std::vector<GraphReconnectPlan> plans;
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (link.fromNodeId != node.id || link.fromSocketId != sourcePlan->toSocketId) {
            continue;
        }
        if (sourcePlan->fromNodeId == link.toNodeId) {
            continue;
        }
        plans.push_back(GraphReconnectPlan{
            sourcePlan->fromNodeId,
            sourcePlan->fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }
    return plans;
}

std::string FileNameFromPath(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path.empty() ? std::string("Image") : path;
    }
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
}

void HashDevelopSubjectImportance(std::size_t& hash, const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    HashCombine(hash, HashValue(importance.enabled));
    HashCombine(hash, HashValue(importance.regions.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        HashCombine(hash, HashValue(region.id));
        HashCombine(hash, HashValue(static_cast<int>(region.mode)));
        HashCombine(hash, HashValue(region.enabled));
        HashCombine(hash, HashValue(region.centerX));
        HashCombine(hash, HashValue(region.centerY));
        HashCombine(hash, HashValue(region.radiusX));
        HashCombine(hash, HashValue(region.radiusY));
        HashCombine(hash, HashValue(region.feather));
        HashCombine(hash, HashValue(region.strength));
    }
    HashCombine(hash, HashValue(importance.strokes.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        HashCombine(hash, HashValue(stroke.id));
        HashCombine(hash, HashValue(static_cast<int>(stroke.mode)));
        HashCombine(hash, HashValue(stroke.enabled));
        HashCombine(hash, HashValue(stroke.subtract));
        HashCombine(hash, HashValue(stroke.radius));
        HashCombine(hash, HashValue(stroke.feather));
        HashCombine(hash, HashValue(stroke.strength));
        HashCombine(hash, HashValue(stroke.points.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            HashCombine(hash, HashValue(point.x));
            HashCombine(hash, HashValue(point.y));
        }
    }
}

std::size_t BuildDevelopAutoSolveTriggerHash(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata) {
    std::size_t hash = HashValue(metadata.sourcePath);
    HashCombine(hash, HashValue(metadata.hasDngBaselineExposure));
    HashCombine(hash, HashValue(metadata.dngBaselineExposure));
    HashCombine(hash, HashValue(metadata.blackLevel));
    HashCombine(hash, HashValue(metadata.whiteLevel));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[2]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[2]));
    HashCombine(hash, HashValue(static_cast<int>(payload.uiMode)));
    HashCombine(hash, HashValue(static_cast<int>(payload.autoGuidance.intent)));
    HashCombine(hash, HashValue(payload.autoGuidance.autoStrength));
    HashCombine(hash, HashValue(payload.autoGuidance.exposureBias));
    HashCombine(hash, HashValue(payload.autoGuidance.dynamicRange));
    HashCombine(hash, HashValue(payload.autoGuidance.shadowLift));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightGuard));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightCharacter));
    HashCombine(hash, HashValue(payload.autoGuidance.contrastBias));
    HashCombine(hash, HashValue(payload.autoGuidance.subjectSceneBias));
    HashCombine(hash, HashValue(payload.autoGuidance.moodReadabilityBias));
    HashDevelopSubjectImportance(hash, payload.subjectImportance);
    // Important: do not hash the live tone-analysis stats that arrive from render feedback.
    // Those stats describe the current authored output, so including them here creates a
    // closed-loop auto solve that can oscillate while the Develop window is open.
    return hash;
}

std::size_t BuildDevelopAutoRawSolveTriggerHash(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata) {
    std::size_t hash = HashValue(metadata.sourcePath);
    HashCombine(hash, HashValue(metadata.hasDngBaselineExposure));
    HashCombine(hash, HashValue(metadata.dngBaselineExposure));
    HashCombine(hash, HashValue(metadata.blackLevel));
    HashCombine(hash, HashValue(metadata.whiteLevel));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[2]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[2]));
    HashCombine(hash, HashValue(static_cast<int>(payload.uiMode)));
    HashCombine(hash, HashValue(static_cast<int>(payload.autoGuidance.intent)));
    HashCombine(hash, HashValue(payload.autoGuidance.autoStrength));
    HashCombine(hash, HashValue(payload.autoGuidance.exposureBias));
    HashCombine(hash, HashValue(payload.autoGuidance.dynamicRange));
    HashCombine(hash, HashValue(payload.autoGuidance.shadowLift));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightGuard));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightCharacter));
    HashCombine(hash, HashValue(payload.autoGuidance.contrastBias));
    HashCombine(hash, HashValue(payload.autoGuidance.subjectSceneBias));
    HashCombine(hash, HashValue(payload.autoGuidance.moodReadabilityBias));
    HashDevelopSubjectImportance(hash, payload.subjectImportance);
    return hash;
}

RenderMaskGeneratorKind ToRenderMaskKind(EditorNodeGraph::MaskGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::Solid: return RenderMaskGeneratorKind::Solid;
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return RenderMaskGeneratorKind::LinearGradient;
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return RenderMaskGeneratorKind::RadialGradient;
        case EditorNodeGraph::MaskGeneratorKind::Noise: return RenderMaskGeneratorKind::Noise;
    }
    return RenderMaskGeneratorKind::Solid;
}

RenderMaskSettings ToRenderMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings) {
    RenderMaskSettings result;
    result.value = settings.value;
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.scale = settings.scale;
    result.centerX = settings.centerX;
    result.centerY = settings.centerY;
    result.radius = settings.radius;
    result.feather = settings.feather;
    result.invert = settings.invert;
    return result;
}

RenderMixBlendMode ToRenderMixBlendMode(EditorNodeGraph::MixBlendMode mode) {
    switch (mode) {
        case EditorNodeGraph::MixBlendMode::Normal: return RenderMixBlendMode::Normal;
        case EditorNodeGraph::MixBlendMode::Average: return RenderMixBlendMode::Average;
        case EditorNodeGraph::MixBlendMode::Add: return RenderMixBlendMode::Add;
        case EditorNodeGraph::MixBlendMode::Multiply: return RenderMixBlendMode::Multiply;
        case EditorNodeGraph::MixBlendMode::Screen: return RenderMixBlendMode::Screen;
        case EditorNodeGraph::MixBlendMode::AlphaOver: return RenderMixBlendMode::AlphaOver;
    }
    return RenderMixBlendMode::Normal;
}

RenderMaskUtilityKind ToRenderMaskUtilityKind(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return RenderMaskUtilityKind::Invert;
        case EditorNodeGraph::MaskUtilityKind::Levels: return RenderMaskUtilityKind::Levels;
        case EditorNodeGraph::MaskUtilityKind::Threshold: return RenderMaskUtilityKind::Threshold;
    }
    return RenderMaskUtilityKind::Invert;
}

RenderMaskCombineMode ToRenderMaskCombineMode(EditorNodeGraph::MaskCombineMode mode) {
    switch (mode) {
        case EditorNodeGraph::MaskCombineMode::Add: return RenderMaskCombineMode::Add;
        case EditorNodeGraph::MaskCombineMode::Subtract: return RenderMaskCombineMode::Subtract;
        case EditorNodeGraph::MaskCombineMode::Intersect: return RenderMaskCombineMode::Intersect;
        case EditorNodeGraph::MaskCombineMode::Exclude: return RenderMaskCombineMode::Exclude;
    }
    return RenderMaskCombineMode::Intersect;
}

RenderCustomMaskObjectType ToRenderCustomMaskObjectType(EditorNodeGraph::CustomMaskObjectType type) {
    switch (type) {
        case EditorNodeGraph::CustomMaskObjectType::Rectangle: return RenderCustomMaskObjectType::Rectangle;
        case EditorNodeGraph::CustomMaskObjectType::Ellipse: return RenderCustomMaskObjectType::Ellipse;
        case EditorNodeGraph::CustomMaskObjectType::Polygon: return RenderCustomMaskObjectType::Polygon;
        case EditorNodeGraph::CustomMaskObjectType::FreeformPath: return RenderCustomMaskObjectType::FreeformPath;
    }
    return RenderCustomMaskObjectType::Rectangle;
}

RenderCustomMaskOperation ToRenderCustomMaskOperation(EditorNodeGraph::CustomMaskOperation operation) {
    switch (operation) {
        case EditorNodeGraph::CustomMaskOperation::Add: return RenderCustomMaskOperation::Add;
        case EditorNodeGraph::CustomMaskOperation::Subtract: return RenderCustomMaskOperation::Subtract;
        case EditorNodeGraph::CustomMaskOperation::Intersect: return RenderCustomMaskOperation::Intersect;
        case EditorNodeGraph::CustomMaskOperation::Exclude: return RenderCustomMaskOperation::Exclude;
    }
    return RenderCustomMaskOperation::Add;
}

RenderCustomMaskPayload ToRenderCustomMaskPayload(const EditorNodeGraph::CustomMaskPayload& payload) {
    RenderCustomMaskPayload result;
    result.width = std::max(1, payload.width);
    result.height = std::max(1, payload.height);
    result.rasterLayer = payload.rasterLayer;
    result.invert = payload.invert;
    result.blurRadius = payload.blurRadius;
    result.expandContract = payload.expandContract;
    result.objects.reserve(payload.objects.size());
    for (const EditorNodeGraph::CustomMaskObject& object : payload.objects) {
        RenderCustomMaskObject renderObject;
        renderObject.id = object.id;
        renderObject.type = ToRenderCustomMaskObjectType(object.type);
        renderObject.operation = ToRenderCustomMaskOperation(object.operation);
        renderObject.enabled = object.enabled;
        renderObject.invert = object.invert;
        renderObject.strength = object.strength;
        renderObject.feather = object.feather;
        renderObject.blur = object.blur;
        renderObject.points.reserve(object.points.size());
        for (const EditorNodeGraph::Vec2& point : object.points) {
            renderObject.points.push_back(RenderCustomMaskPoint{ point.x, point.y });
        }
        result.objects.push_back(std::move(renderObject));
    }
    return result;
}

EditorNodeGraph::MaskCombineMode ToGraphMaskCombineMode(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return EditorNodeGraph::MaskCombineMode::Add;
        case ToneCurveScopeMaskAction::Subtract: return EditorNodeGraph::MaskCombineMode::Subtract;
        case ToneCurveScopeMaskAction::Intersect:
        case ToneCurveScopeMaskAction::NewMask:
        default: return EditorNodeGraph::MaskCombineMode::Intersect;
    }
}

RenderImageToMaskKind ToRenderImageToMaskKind(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return RenderImageToMaskKind::Luminance;
        case EditorNodeGraph::ImageToMaskKind::SampledRange: return RenderImageToMaskKind::SampledRange;
    }
    return RenderImageToMaskKind::Luminance;
}

RenderImageGeneratorKind ToRenderImageGeneratorKind(EditorNodeGraph::ImageGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageGeneratorKind::SolidColor: return RenderImageGeneratorKind::SolidColor;
        case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return RenderImageGeneratorKind::ColorGradient;
        case EditorNodeGraph::ImageGeneratorKind::Square: return RenderImageGeneratorKind::Square;
        case EditorNodeGraph::ImageGeneratorKind::Circle: return RenderImageGeneratorKind::Circle;
        case EditorNodeGraph::ImageGeneratorKind::Text: return RenderImageGeneratorKind::Text;
    }
    return RenderImageGeneratorKind::SolidColor;
}

RenderMaskUtilitySettings ToRenderMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings) {
    RenderMaskUtilitySettings result;
    result.blackPoint = settings.blackPoint;
    result.whitePoint = settings.whitePoint;
    result.gamma = settings.gamma;
    result.threshold = settings.threshold;
    result.softness = settings.softness;
    result.invert = settings.invert;
    return result;
}

RenderImageToMaskSettings ToRenderImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    RenderImageToMaskSettings result;
    result.low = settings.low;
    result.high = settings.high;
    result.softness = settings.softness;
    result.invert = settings.invert;
    result.sampleCount = std::clamp(settings.sampleCount, 1, 5);
    result.sampleRgb[0] = settings.sampleRgb[0];
    result.sampleRgb[1] = settings.sampleRgb[1];
    result.sampleRgb[2] = settings.sampleRgb[2];
    result.sampleLuma = settings.sampleLuma;
    for (int i = 0; i < 4; ++i) {
        result.extraSampleRgb[i][0] = settings.extraSampleRgb[i][0];
        result.extraSampleRgb[i][1] = settings.extraSampleRgb[i][1];
        result.extraSampleRgb[i][2] = settings.extraSampleRgb[i][2];
        result.extraSampleLuma[i] = settings.extraSampleLuma[i];
    }
    result.sampleU = settings.sampleU;
    result.sampleV = settings.sampleV;
    result.toneSimilarity = settings.toneSimilarity;
    result.colorSimilarity = settings.colorSimilarity;
    result.regionRadius = settings.regionRadius;
    result.regionFeather = settings.regionFeather;
    result.edgeSensitivity = settings.edgeSensitivity;
    result.localCoherence = settings.localCoherence;
    return result;
}

RenderImageGeneratorSettings ToRenderImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings) {
    RenderImageGeneratorSettings result;
    for (int i = 0; i < 4; ++i) {
        result.colorA[i] = settings.colorA[i];
        result.colorB[i] = settings.colorB[i];
    }
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.text = settings.text;
    result.fontSize = settings.fontSize;
    return result;
}

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
        kind == EditorNodeGraph::NodeKind::MaskCombine ||
        kind == EditorNodeGraph::NodeKind::MaskUtility ||
        kind == EditorNodeGraph::NodeKind::CustomMask ||
        kind == EditorNodeGraph::NodeKind::ImageToMask ||
        kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        kind == EditorNodeGraph::NodeKind::RawDetailFusion;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
        kind == EditorNodeGraph::NodeKind::RawDevelop ||
        kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
        kind == EditorNodeGraph::NodeKind::HdrMerge ||
        kind == EditorNodeGraph::NodeKind::ImageGenerator ||
        kind == EditorNodeGraph::NodeKind::Layer ||
        kind == EditorNodeGraph::NodeKind::Mix ||
        kind == EditorNodeGraph::NodeKind::DataMath ||
        kind == EditorNodeGraph::NodeKind::Output;
}

bool ConnectionUsesImageAsRenderSource(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to || from->kind != EditorNodeGraph::NodeKind::Image ||
        fromSocketId != EditorNodeGraph::kImageOutputSocketId) {
        return false;
    }

    if (to->kind == EditorNodeGraph::NodeKind::Layer && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::RawDetailFusion && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::HdrMerge &&
        toSocketId == EditorNodeGraph::kHdrMergeInput1SocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Output && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Mix &&
        (toSocketId == EditorNodeGraph::kMixInputASocketId ||
         toSocketId == EditorNodeGraph::kMixInputBSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::DataMath &&
        (toSocketId == EditorNodeGraph::kMixInputASocketId ||
         toSocketId == EditorNodeGraph::kMixInputBSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::ChannelSplit &&
        toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    return false;
}

float SmoothStep(float edge0, float edge1, float x) {
    if (std::abs(edge1 - edge0) < 0.00001f) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float MaskPreviewValue(const EditorNodeGraph::Node& node, float u, float v) {
    const EditorNodeGraph::MaskGeneratorSettings& settings = node.maskSettings;
    float value = settings.value;
    if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
        constexpr float kPi = 3.14159265358979323846f;
        const float radians = settings.angle * kPi / 180.0f;
        const float dx = std::cos(radians);
        const float dy = std::sin(radians);
        value = ((u - 0.5f) * dx + (v - 0.5f) * dy) * settings.scale + 0.5f + settings.offset;
        value = std::clamp(value, 0.0f, 1.0f);
    } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient) {
        const float dx = u - settings.centerX;
        const float dy = v - settings.centerY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        value = 1.0f - SmoothStep(std::max(0.0f, settings.radius - settings.feather), settings.radius + settings.feather, dist);
    }
    if (settings.invert) {
        value = 1.0f - value;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

std::vector<unsigned char> GenerateMaskPreviewPixels(const EditorNodeGraph::Node& node, int& outW, int& outH) {
    outW = 192;
    outH = 128;
    std::vector<unsigned char> pixels(static_cast<size_t>(outW * outH * 4), 255);
    for (int y = 0; y < outH; ++y) {
        const float v = outH > 1 ? static_cast<float>(y) / static_cast<float>(outH - 1) : 0.0f;
        for (int x = 0; x < outW; ++x) {
            const float u = outW > 1 ? static_cast<float>(x) / static_cast<float>(outW - 1) : 0.0f;
            const unsigned char gray = static_cast<unsigned char>(std::round(MaskPreviewValue(node, u, v) * 255.0f));
            const size_t dst = static_cast<size_t>(y * outW + x) * 4;
            pixels[dst + 0] = gray;
            pixels[dst + 1] = gray;
            pixels[dst + 2] = gray;
            pixels[dst + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

EditorModule::EditorModule() {}

EditorModule::~EditorModule() {
    ClearCompositeSceneTextures();
}

EditorNodeGraph::Graph& EditorModule::GetNodeGraph() {
    return m_NodeGraph;
}

const EditorNodeGraph::Graph& EditorModule::GetNodeGraph() const {
    return m_NodeGraph;
}

bool EditorModule::GetDevelopSubjectImportanceViewportState(DevelopSubjectViewportState& outState) const {
    outState = DevelopSubjectViewportState{};
    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    const EditorNodeGraph::Node* node = selectedNodeId > 0 ? m_NodeGraph.FindNode(selectedNodeId) : nullptr;
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    const EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    if (!importance.enabled ||
        !importance.showOverlay ||
        (importance.regions.empty() && importance.strokes.empty() && !importance.brushEnabled)) {
        return false;
    }

    outState.nodeId = node->id;
    outState.enabled = importance.enabled;
    outState.showOverlay = importance.showOverlay;
    outState.overlayOpacity = importance.overlayOpacity;
    outState.showInterpretedMapOverlay = importance.showInterpretedMapOverlay;
    outState.interpretedMapOpacity = importance.interpretedMapOpacity;
    outState.showRefinedMapOverlay = importance.showRefinedMapOverlay;
    outState.refinedMapOpacity = importance.refinedMapOpacity;
    outState.brushEnabled = importance.brushEnabled;
    outState.brushSubtract = importance.brushSubtract;
    outState.brushMode = importance.brushMode;
    outState.brushRadius = importance.brushRadius;
    outState.brushFeather = importance.brushFeather;
    outState.brushStrength = importance.brushStrength;
    outState.activeRegionId = importance.activeRegionId;
    outState.activeStrokeId = importance.activeStrokeId;
    outState.regions.reserve(importance.regions.size());
    outState.strokes.reserve(importance.strokes.size());

    int firstRegionId = 0;
    bool activeRegionFound = false;
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (firstRegionId == 0) {
            firstRegionId = region.id;
        }
        activeRegionFound = activeRegionFound || region.id == importance.activeRegionId;
        DevelopSubjectViewportRegion viewportRegion;
        viewportRegion.id = region.id;
        viewportRegion.mode = region.mode;
        viewportRegion.enabled = region.enabled;
        viewportRegion.centerX = region.centerX;
        viewportRegion.centerY = region.centerY;
        viewportRegion.radiusX = region.radiusX;
        viewportRegion.radiusY = region.radiusY;
        viewportRegion.feather = region.feather;
        viewportRegion.strength = region.strength;
        outState.regions.push_back(viewportRegion);
    }
    if (!activeRegionFound) {
        outState.activeRegionId = firstRegionId;
    }

    int firstStrokeId = 0;
    bool activeStrokeFound = false;
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (firstStrokeId == 0) {
            firstStrokeId = stroke.id;
        }
        activeStrokeFound = activeStrokeFound || stroke.id == importance.activeStrokeId;
        DevelopSubjectViewportStroke viewportStroke;
        viewportStroke.id = stroke.id;
        viewportStroke.mode = stroke.mode;
        viewportStroke.enabled = stroke.enabled;
        viewportStroke.subtract = stroke.subtract;
        viewportStroke.radius = stroke.radius;
        viewportStroke.feather = stroke.feather;
        viewportStroke.strength = stroke.strength;
        viewportStroke.points.reserve(stroke.points.size());
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            viewportStroke.points.push_back({ point.x, point.y });
        }
        outState.strokes.push_back(std::move(viewportStroke));
    }
    if (!activeStrokeFound) {
        outState.activeStrokeId = firstStrokeId;
    }

    if (importance.showInterpretedMapOverlay) {
        const DevelopSubjectImportanceInterpretation interpretation =
            InterpretDevelopSubjectImportanceMap(importance);
        outState.interpretedMapActive =
            interpretation.active &&
            interpretation.coverage > 0.001f &&
            interpretation.gridWidth > 0 &&
            interpretation.gridHeight > 0;
        if (outState.interpretedMapActive) {
            outState.interpretedMapGridWidth = interpretation.gridWidth;
            outState.interpretedMapGridHeight = interpretation.gridHeight;
            outState.interpretedMapCells.reserve(interpretation.cells.size());
            for (const DevelopSubjectImportanceMapCell& cell : interpretation.cells) {
                DevelopSubjectViewportMapCell viewportCell;
                viewportCell.importance = cell.importance;
                viewportCell.reveal = cell.reveal;
                viewportCell.protect = cell.protect;
                viewportCell.preserveMood = cell.preserveMood;
                viewportCell.lowPriority = cell.lowPriority;
                outState.interpretedMapCells.push_back(viewportCell);
            }
        }
    }

    if (importance.showRefinedMapOverlay) {
        auto appendRefinedMapCellFromJson = [&](const nlohmann::json& cellJson) {
            DevelopSubjectViewportMapCell viewportCell;
            viewportCell.importance = cellJson.value("importance", 0.0f);
            viewportCell.reveal = cellJson.value("readability", 0.0f);
            viewportCell.protect = cellJson.value("protection", 0.0f);
            viewportCell.preserveMood = cellJson.value("preserveMood", 0.0f);
            viewportCell.lowPriority = cellJson.value("lowPriority", 0.0f);
            viewportCell.confidence = cellJson.value("confidence", 0.0f);
            viewportCell.boundaryHint = cellJson.value("boundaryHint", 0.0f);
            outState.refinedMapCells.push_back(viewportCell);
        };
        const nlohmann::json solvedRefinedMap =
            node->rawDevelop.integratedToneLayerJson.value(
                "autoSubjectSceneRefinedMap",
                nlohmann::json::object());
        const nlohmann::json solvedRefinedCells =
            solvedRefinedMap.value("cells", nlohmann::json::array());
        if (solvedRefinedMap.value("version", std::string()) == kDevelopSubjectRefinedMapVersion &&
            solvedRefinedMap.value("active", false) &&
            solvedRefinedCells.is_array() &&
            !solvedRefinedCells.empty()) {
            outState.refinedMapActive = true;
            outState.refinedMapGridWidth = solvedRefinedMap.value("gridWidth", 0);
            outState.refinedMapGridHeight = solvedRefinedMap.value("gridHeight", 0);
            outState.refinedMapCells.reserve(solvedRefinedCells.size());
            for (const nlohmann::json& cellJson : solvedRefinedCells) {
                if (cellJson.is_object()) {
                    appendRefinedMapCellFromJson(cellJson);
                }
            }
            outState.refinedMapActive =
                outState.refinedMapGridWidth > 0 &&
                outState.refinedMapGridHeight > 0 &&
                !outState.refinedMapCells.empty();
        }
        if (!outState.refinedMapActive) {
            const DevelopSubjectImportanceInterpretation interpretation =
                InterpretDevelopSubjectImportanceMap(importance);
            DevelopSubjectSceneIntent fallbackIntent;
            fallbackIntent.userGuidanceStrength = interpretation.mapConfidence;
            fallbackIntent.subjectPriority = SaturateFloat(
                0.45f +
                interpretation.positiveCoverage * 0.20f +
                interpretation.centerBias * 0.12f);
            fallbackIntent.sceneIntegrity = SaturateFloat(
                0.45f +
                interpretation.lowPriorityCoverage * 0.16f +
                interpretation.edgeBias * 0.10f);
            fallbackIntent.improveReadability = SaturateFloat(
                0.42f + interpretation.revealCoverage * 0.30f);
            fallbackIntent.preserveMood = SaturateFloat(
                0.42f +
                interpretation.moodCoverage * 0.24f +
                interpretation.lowPriorityCoverage * 0.12f);
            fallbackIntent.protectionPressure = SaturateFloat(
                interpretation.protectCoverage * 0.36f);
            const DevelopSubjectRefinedMap refinedMap =
                BuildDevelopSubjectRefinedMap(interpretation, fallbackIntent);
            outState.refinedMapActive =
                refinedMap.active &&
                (refinedMap.coverage > 0.001f || refinedMap.lowPriorityCoverage > 0.001f);
            if (outState.refinedMapActive) {
                outState.refinedMapGridWidth = refinedMap.gridWidth;
                outState.refinedMapGridHeight = refinedMap.gridHeight;
                outState.refinedMapCells.reserve(refinedMap.cells.size());
                for (const DevelopSubjectRefinedMapCell& cell : refinedMap.cells) {
                    DevelopSubjectViewportMapCell viewportCell;
                    viewportCell.importance = cell.importance;
                    viewportCell.reveal = cell.readability;
                    viewportCell.protect = cell.protection;
                    viewportCell.preserveMood = cell.preserveMood;
                    viewportCell.lowPriority = cell.lowPriority;
                    viewportCell.confidence = cell.confidence;
                    viewportCell.boundaryHint = cell.boundaryHint;
                    outState.refinedMapCells.push_back(viewportCell);
                }
            }
        }
    }

    return outState.brushEnabled ||
        !outState.regions.empty() ||
        !outState.strokes.empty() ||
        outState.interpretedMapActive ||
        outState.refinedMapActive;
}

bool EditorModule::SetDevelopSubjectImportanceActiveRegion(int nodeId, int regionId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || regionId <= 0) {
        return false;
    }
    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    const auto regionIt = std::find_if(
        importance.regions.begin(),
        importance.regions.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceRegion& region) {
            return region.id == regionId;
        });
    if (regionIt == importance.regions.end() || importance.activeRegionId == regionId) {
        return false;
    }
    importance.activeRegionId = regionId;
    m_Dirty = true;
    return true;
}

bool EditorModule::UpdateDevelopSubjectImportanceRegionFromViewport(
    int nodeId,
    int regionId,
    float centerX,
    float centerY,
    float radiusX,
    float radiusY) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || regionId <= 0) {
        return false;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    auto regionIt = std::find_if(
        importance.regions.begin(),
        importance.regions.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceRegion& region) {
            return region.id == regionId;
        });
    if (regionIt == importance.regions.end()) {
        return false;
    }

    const float nextCenterX = std::clamp(centerX, 0.0f, 1.0f);
    const float nextCenterY = std::clamp(centerY, 0.0f, 1.0f);
    const float nextRadiusX = std::clamp(radiusX, 0.01f, 1.0f);
    const float nextRadiusY = std::clamp(radiusY, 0.01f, 1.0f);
    const bool geometryChanged =
        std::abs(regionIt->centerX - nextCenterX) > 0.0001f ||
        std::abs(regionIt->centerY - nextCenterY) > 0.0001f ||
        std::abs(regionIt->radiusX - nextRadiusX) > 0.0001f ||
        std::abs(regionIt->radiusY - nextRadiusY) > 0.0001f;
    const bool activeChanged = importance.activeRegionId != regionId;
    if (!geometryChanged && !activeChanged) {
        return false;
    }

    regionIt->centerX = nextCenterX;
    regionIt->centerY = nextCenterY;
    regionIt->radiusX = nextRadiusX;
    regionIt->radiusY = nextRadiusY;
    importance.enabled = true;
    importance.showOverlay = true;
    importance.activeRegionId = regionId;
    NormalizeDevelopSubjectImportance(importance);

    if (geometryChanged) {
        RecordRawDevelopInteraction(nodeId);
        const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *node);
        if (node->rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Auto &&
            rawSourceNode &&
            rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
            (void)UpdateDevelopAutoState(nodeId, node->rawDevelop, rawSourceNode->rawSource.metadata, true, false);
        }
        MarkRenderDirty(nodeId);
    } else {
        m_Dirty = true;
    }
    return true;
}

int EditorModule::BeginDevelopSubjectImportanceBrushStroke(int nodeId, float x, float y) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return 0;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    if (!importance.brushEnabled) {
        return 0;
    }

    EditorNodeGraph::DevelopSubjectImportanceStroke stroke;
    stroke.id = std::max(1, importance.nextStrokeId++);
    stroke.mode = importance.brushMode;
    stroke.enabled = true;
    stroke.subtract = importance.brushSubtract;
    stroke.radius = importance.brushRadius;
    stroke.feather = importance.brushFeather;
    stroke.strength = importance.brushStrength;
    stroke.points.push_back({
        std::clamp(x, 0.0f, 1.0f),
        std::clamp(y, 0.0f, 1.0f)
    });

    importance.enabled = true;
    importance.showOverlay = true;
    importance.activeStrokeId = stroke.id;
    importance.strokes.push_back(std::move(stroke));
    NormalizeDevelopSubjectImportance(importance);
    RecordRawDevelopInteraction(nodeId);
    m_Dirty = true;
    return importance.activeStrokeId;
}

bool EditorModule::AppendDevelopSubjectImportanceBrushStroke(int nodeId, int strokeId, float x, float y) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || strokeId <= 0) {
        return false;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    auto strokeIt = std::find_if(
        importance.strokes.begin(),
        importance.strokes.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
            return stroke.id == strokeId;
        });
    if (strokeIt == importance.strokes.end()) {
        return false;
    }

    const float nextX = std::clamp(x, 0.0f, 1.0f);
    const float nextY = std::clamp(y, 0.0f, 1.0f);
    RecordRawDevelopInteraction(nodeId);
    if (!strokeIt->points.empty()) {
        const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& last = strokeIt->points.back();
        const float dx = nextX - last.x;
        const float dy = nextY - last.y;
        const float minDistance = std::max(strokeIt->radius * 0.35f, 0.003f);
        if (dx * dx + dy * dy < minDistance * minDistance) {
            return false;
        }
    }

    if (strokeIt->points.size() >= 192) {
        strokeIt->points.erase(strokeIt->points.begin());
    }
    strokeIt->points.push_back({ nextX, nextY });
    importance.activeStrokeId = strokeId;
    m_Dirty = true;
    return true;
}

bool EditorModule::EndDevelopSubjectImportanceBrushStroke(int nodeId, int strokeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || strokeId <= 0) {
        return false;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    const auto strokeIt = std::find_if(
        importance.strokes.begin(),
        importance.strokes.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
            return stroke.id == strokeId;
        });
    if (strokeIt == importance.strokes.end()) {
        return false;
    }

    importance.enabled = true;
    importance.showOverlay = true;
    importance.activeStrokeId = strokeId;
    NormalizeDevelopSubjectImportance(importance);
    RecordRawDevelopInteraction(nodeId);

    const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *node);
    if (node->rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Auto &&
        rawSourceNode &&
        rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        (void)UpdateDevelopAutoState(nodeId, node->rawDevelop, rawSourceNode->rawSource.metadata, true, false);
    }
    MarkRenderDirty(nodeId);
    return true;
}

bool EditorModule::IsGraphOutputConnected() const {
    return m_NodeGraph.IsOutputConnected();
}

void EditorModule::ClearCompositeSelection() {
    m_CompositeSelectedOutputNodeId = -1;
    m_NodeGraph.ClearSelection();
}

void EditorModule::Initialize(GLFWwindow* sharedWindow, StackAppearance::AppearanceManager* appearance) {
    m_Appearance = appearance;
    std::vector<std::string> registryErrors;
    if (!LayerRegistry::ValidateRegistry(&registryErrors)) {
        for (const std::string& error : registryErrors) {
            fprintf(stderr, "LayerRegistry validation failed: %s\n", error.c_str());
        }
    }

    m_Pipeline.Initialize();
    m_CompositePreviewPipeline.Initialize();
    m_Sidebar.Initialize();
    m_Viewport.Initialize();
    m_Scopes.Initialize();
    m_RenderWorkerAvailable = sharedWindow && m_RenderWorker.Initialize(sharedWindow);

    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_CanvasToolKind = CanvasToolKind::None;
    m_CanvasToolOwnerNodeId = -1;
    m_CanvasToolStatusText.clear();
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
    m_NodeGraph.ResetFromLayers(0, false);
    ClearCompositeRuntimeState();
    MarkRenderDirty();

    m_Dirty = false;
    m_LastUserActionTime = 0.0;
    m_LastAutoSaveTime = -1.0;
}

void EditorModule::RequestNewProject() {
    if (!HasProjectContent()) {
        ResetToBlankProject();
        return;
    }
    m_ShowNewProjectPrompt = true;
}

bool EditorModule::HasProjectContent() const {
    return !m_CurrentProjectName.empty() ||
        !m_CurrentProjectFileName.empty() ||
        !m_Layers.empty() ||
        !m_NodeGraph.GetNodes().empty() ||
        !m_NodeGraph.GetLinks().empty() ||
        m_Pipeline.HasSourceImage();
}

bool EditorModule::ConsumeUiNotification(UiNotificationEvent& outEvent) {
    if (m_UiNotifications.empty()) {
        return false;
    }
    outEvent = std::move(m_UiNotifications.front());
    m_UiNotifications.pop_front();
    return true;
}

void EditorModule::QueueUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    if (message.empty()) {
        return;
    }
    if (!dedupeKey.empty()) {
        for (UiNotificationEvent& event : m_UiNotifications) {
            if (event.dedupeKey == dedupeKey) {
                event.severity = severity;
                event.message = std::move(message);
                return;
            }
        }
    }
    m_UiNotifications.push_back(UiNotificationEvent{
        severity,
        std::move(message),
        std::move(dedupeKey)
    });
}

void EditorModule::ResetToBlankProject() {
    CancelCanvasTool();
    CancelGraphAutoFocusTracking();
    m_ShowNewProjectPrompt = false;
    m_ShowNewProjectDiscardConfirm = false;
    m_GraphDropImportTaskState = Async::TaskState::Idle;
    m_GraphDropImportStatusText.clear();
    m_PendingGraphDropImports.clear();
    m_SourceLoadTaskState = Async::TaskState::Idle;
    m_SourceLoadStatusText.clear();
    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_NodeGraph.ResetFromLayers(0, false);
    ClearCompositeRuntimeState();
    m_NodeDirtyGenerations.clear();
    m_DevelopAutoSolveTriggerHashes.clear();
    m_DevelopAutoRawSolveTriggerHashes.clear();
    m_DevelopAutoRawCalibrationHashes.clear();
    m_DevelopAutoGuidanceDrafts.clear();
    m_RawDevelopExposureDrafts.clear();
    m_LastRawDevelopInteractionTime = -1.0;
    m_RawDevelopInteractionSerialCounter = 1;
    m_RawDevelopInteractionTimes.clear();
    m_RawDevelopInteractionSerials.clear();
    m_DeferredDevelopCandidateFeedbackTimes.clear();
    m_PreviewDisplayedRevisions.clear();
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_HdrMergeRequestedGenerations.clear();
    m_HdrMergeCompletedGenerations.clear();
    m_HdrMergeFailureMessages.clear();
    m_HdrMergeRenderingNodeIds.clear();
    m_HdrMergeSubmittedNodesByGeneration.clear();
    m_ScopeDisplayedRevisions.clear();
    ResetRenderSubmissionState();
    m_Pipeline.Clear();
    m_CompositePreviewPipeline.Clear();
    SetCurrentProjectName("");
    SetCurrentProjectFileName("");
    MarkRenderDirty();
    ClearDirty();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_LastAutoSaveTime = -1.0;
}

void EditorModule::ResetRenderSubmissionState() {
    ++m_RenderGeneration;
    m_RenderPending = false;
    m_RenderDirty = true;
    m_LastCompletedRenderGeneration = 0;
    m_LastSubmittedRenderRevision = 0;
    m_HdrMergeRequestedGenerations.clear();
    m_HdrMergeCompletedGenerations.clear();
    m_HdrMergeFailureMessages.clear();
    m_HdrMergeRenderingNodeIds.clear();
    m_HdrMergeSubmittedNodesByGeneration.clear();
    m_Viewport.ResetSinglePreviewState();
    m_HoverFade = 0.0f;
}

void EditorModule::RenderProjectLifecyclePopups() {
    if (m_ShowNewProjectPrompt) {
        ImGui::OpenPopup("Start New Project##Editor");
        m_ShowNewProjectPrompt = false;
    }
    if (m_ShowNewProjectDiscardConfirm) {
        ImGui::OpenPopup("Confirm Discard Project##Editor");
        m_ShowNewProjectDiscardConfirm = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Start New Project##Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Start a new project? You can save the current project first, continue without saving, or cancel.");
        ImGui::Spacing();

        if (ImGui::Button("Save Project", ImVec2(140.0f, 0.0f))) {
            const std::string projectName = m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName;
            LibraryManager::Get().RequestSaveProject(projectName, this, m_CurrentProjectFileName);
            ResetToBlankProject();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(140.0f, 0.0f))) {
            m_ShowNewProjectDiscardConfirm = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Discard Project##Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Are you sure you want to discard the current project and start a new blank one?");
        ImGui::Spacing();

        if (ImGui::Button("Yes, Discard", ImVec2(140.0f, 0.0f))) {
            ResetToBlankProject();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorModule::AddLayer(LayerType type) {
    std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayer(type);

    if (newLayer) {
        newLayer->InitializeGL();

        const char* defaultName = newLayer->GetDefaultName();
        int count = 0;
        for (const auto& existing : m_Layers) {
            if (strcmp(existing->GetDefaultName(), defaultName) == 0) {
                count++;
            }
        }

        if (count > 0) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix), "%s (%d)", defaultName, count + 1);
            newLayer->SetInstanceName(suffix);
        }

        m_Layers.push_back(newLayer);
        SelectLayer(static_cast<int>(m_Layers.size()) - 1);
        m_FocusSelectedTabNextRender = true;
        MarkRenderDirty();
    }
}

void EditorModule::AddLayerNodeAt(LayerType type, EditorNodeGraph::Vec2 graphPosition) {
    const int layerIndex = static_cast<int>(m_Layers.size());
    AddLayer(type);
    if (static_cast<int>(m_Layers.size()) == layerIndex + 1) {
        m_NodeGraph.AddLayerNode(type, layerIndex, graphPosition);
        RefreshGraphLayerMetadata();
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(layerIndex)) {
            SelectGraphNode(node->id);
        }
        MarkRenderDirty();
    }
}

void EditorModule::RemoveLayer(int index) {
    if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
        if (m_CanvasToolOwnerNodeId > 0) {
            const EditorNodeGraph::Node* ownerNode = m_NodeGraph.FindNode(m_CanvasToolOwnerNodeId);
            if (ownerNode && ownerNode->kind == EditorNodeGraph::NodeKind::Layer && ownerNode->layerIndex == index) {
                CancelCanvasTool();
            }
        }
        // TODO: Promote this to an undoable editor command when command history lands.
        m_Layers.erase(m_Layers.begin() + index);
        m_NodeGraph.RemoveLayerNode(index);
        RefreshGraphLayerMetadata();
        if (m_SelectedLayerIndex >= static_cast<int>(m_Layers.size())) {
            m_SelectedLayerIndex = static_cast<int>(m_Layers.size()) - 1;
        }
        MarkRenderDirty();
    }
}

void EditorModule::MoveLayer(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= static_cast<int>(m_Layers.size())) return;
    if (to < 0 || to >= static_cast<int>(m_Layers.size())) return;

    // TODO: Promote this to an undoable editor command when command history lands.
    if (from < to) {
        std::rotate(m_Layers.begin() + from, m_Layers.begin() + from + 1, m_Layers.begin() + to + 1);
    } else {
        std::rotate(m_Layers.begin() + to, m_Layers.begin() + from, m_Layers.begin() + from + 1);
    }

    if (m_SelectedLayerIndex == from) {
        m_SelectedLayerIndex = to;
    } else if (from < m_SelectedLayerIndex && to >= m_SelectedLayerIndex) {
        m_SelectedLayerIndex--;
    } else if (from > m_SelectedLayerIndex && to <= m_SelectedLayerIndex) {
        m_SelectedLayerIndex++;
    }

    RefreshGraphLayerMetadata();
    MarkRenderDirty();
}

void EditorModule::SetLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    // TODO: Promote this to an undoable editor command when command history lands.
    m_Layers[index]->SetVisible(visible);
    MarkRenderDirty();
}

void EditorModule::SelectLayer(int index) {
    if (index < -1 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    m_SelectedLayerIndex = index;
}

void EditorModule::SelectGraphNode(int nodeId) {
    m_NodeGraph.SelectNode(nodeId);
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectLayer(node->layerIndex);
    } else {
        SelectLayer(-1);
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::Output) {
        m_CompositeSelectedOutputNodeId = nodeId;
    }
}

bool EditorModule::LayerUsesRichNodeSurface(int layerIndex) const {
    // Rich expanded surface layers now render as clean, compact nodes in the graph
    // and open their settings in dedicated settings pages instead of expanding on-graph.
    return false;
}

bool EditorModule::NodeUsesRichNodeSurface(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    return node &&
        ((node->kind == EditorNodeGraph::NodeKind::Layer && LayerUsesRichNodeSurface(node->layerIndex)) ||
         node->kind == EditorNodeGraph::NodeKind::RawSource ||
         node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
         node->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
         node->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         node->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         node->kind == EditorNodeGraph::NodeKind::CustomMask);
}

NodeSurfaceSpec EditorModule::GetLayerNodeSurfaceSpec(int layerIndex) const {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(m_Layers.size()) || !m_Layers[layerIndex]) {
        return {};
    }
    return m_Layers[layerIndex]->GetNodeSurfaceSpec();
}

NodeSurfaceSpec EditorModule::GetNodeSurfaceSpec(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return {};
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawSource) {
        NodeSurfaceSpec spec;
        spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
        spec.density = NodeSurfaceDensity::Dense;
        spec.preferredWidth = 420.0f;
        spec.maxWidth = 520.0f;
        return spec;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
        node->kind == EditorNodeGraph::NodeKind::RawDevelop ||
        node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        node->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
        node->kind == EditorNodeGraph::NodeKind::HdrMerge) {
        NodeSurfaceSpec spec;
        spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
        spec.density = NodeSurfaceDensity::Dense;
        spec.preferredWidth = 420.0f;
        spec.maxWidth = 520.0f;
        return spec;
    }
    if (node->kind != EditorNodeGraph::NodeKind::Layer) {
        return {};
    }
    return GetLayerNodeSurfaceSpec(node->layerIndex);
}

void EditorModule::BeginCanvasColorPick(
    int ownerNodeId,
    const std::string& statusText,
    std::function<void(float, float, float)> callback) {
    m_CanvasToolKind = CanvasToolKind::PickColor;
    m_CanvasToolOwnerNodeId = ownerNodeId;
    m_CanvasToolStatusText = statusText.empty() ? "Click canvas to sample color" : statusText;
    m_IsPickingColor = true;
    m_ColorPickerCallback = std::move(callback);
}

void EditorModule::BeginToneCurveTargeting(int ownerNodeId, const std::string& statusText) {
    m_CanvasToolKind = CanvasToolKind::ToneCurveTarget;
    m_CanvasToolOwnerNodeId = ownerNodeId;
    m_CanvasToolStatusText = statusText.empty()
        ? "Click and drag in the main viewport to adjust the sampled tone"
        : statusText;
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
}

void EditorModule::CancelCanvasTool() {
    if (m_CanvasToolKind == CanvasToolKind::ToneCurveTarget) {
        EndToneCurveViewportTargetDrag();
        ClearTrackedToneCurveProbe();
    }
    m_CanvasToolKind = CanvasToolKind::None;
    m_CanvasToolOwnerNodeId = -1;
    m_CanvasToolStatusText.clear();
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
}

void EditorModule::RestoreIntegratedToneTransientState(int ownerNodeId, ToneCurveLayer& toneCurve) const {
    const auto it = m_IntegratedToneViewportInteractionCache.find(ownerNodeId);
    if (it == m_IntegratedToneViewportInteractionCache.end()) {
        toneCurve.RestoreViewportInteractionState(ToneCurveLayer::ViewportInteractionState{});
        return;
    }

    ToneCurveLayer::ViewportInteractionState state;
    state.probeValid = it->second.probeValid;
    state.probeSamplingBasis = static_cast<ToneCurveSamplingBasis>(std::clamp(it->second.probeSamplingBasis, 0, 1));
    state.probeU = it->second.probeU;
    state.probeV = it->second.probeV;
    state.probeRgba = it->second.probeRgba;
    state.selectionSeedValid = it->second.selectionSeedValid;
    state.selectionSeedU = it->second.selectionSeedU;
    state.selectionSeedV = it->second.selectionSeedV;
    state.selectionSeedInputX = it->second.selectionSeedInputX;
    state.selectionSeedSceneValue = it->second.selectionSeedSceneValue;
    state.selectionSeedRgba = it->second.selectionSeedRgba;
    state.onImageDragPointIndex = it->second.onImageDragPointIndex;
    state.onImageDragAnchorInputX = it->second.onImageDragAnchorInputX;
    state.onImageDragAnchorOutputY = it->second.onImageDragAnchorOutputY;
    toneCurve.RestoreViewportInteractionState(state);
}

void EditorModule::StoreIntegratedToneTransientState(int ownerNodeId, const ToneCurveLayer& toneCurve) const {
    ToneCurveViewportInteractionCache cache;
    const ToneCurveLayer::ViewportInteractionState state = toneCurve.CaptureViewportInteractionState();
    cache.probeValid = state.probeValid;
    cache.probeSamplingBasis = static_cast<int>(state.probeSamplingBasis);
    cache.probeU = state.probeU;
    cache.probeV = state.probeV;
    cache.probeRgba = state.probeRgba;
    cache.selectionSeedValid = state.selectionSeedValid;
    cache.selectionSeedU = state.selectionSeedU;
    cache.selectionSeedV = state.selectionSeedV;
    cache.selectionSeedInputX = state.selectionSeedInputX;
    cache.selectionSeedSceneValue = state.selectionSeedSceneValue;
    cache.selectionSeedRgba = state.selectionSeedRgba;
    cache.onImageDragPointIndex = state.onImageDragPointIndex;
    cache.onImageDragAnchorInputX = state.onImageDragAnchorInputX;
    cache.onImageDragAnchorOutputY = state.onImageDragAnchorOutputY;
    m_IntegratedToneViewportInteractionCache[ownerNodeId] = cache;
}

void EditorModule::ClearIntegratedToneTransientState(int ownerNodeId) const {
    m_IntegratedToneViewportInteractionCache.erase(ownerNodeId);
}

void EditorModule::OnCanvasColorPicked(float r, float g, float b) {
    if (m_ColorPickerCallback) {
        m_ColorPickerCallback(r, g, b);
    }
    CancelCanvasTool();
}

bool EditorModule::SelectAdjacentMainChainNode(int direction) {
    if (direction == 0) {
        return false;
    }

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (selectedNodeId <= 0) {
        return false;
    }

    const int adjacentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(selectedNodeId, direction);
    if (adjacentNodeId <= 0 || adjacentNodeId == selectedNodeId) {
        return false;
    }

    SelectGraphNode(adjacentNodeId);
    return true;
}

void EditorModule::ApplyGraphLayerOrder() {
    const std::vector<int> order = m_NodeGraph.GetRenderLayerIndexPath();
    if (order.empty()) {
        return;
    }

    std::vector<int> uniqueOrder;
    for (int index : order) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size()) &&
            std::find(uniqueOrder.begin(), uniqueOrder.end(), index) == uniqueOrder.end()) {
            uniqueOrder.push_back(index);
        }
    }
    if (uniqueOrder.size() != m_Layers.size()) {
        return;
    }

    std::vector<std::shared_ptr<LayerBase>> reordered;
    reordered.reserve(m_Layers.size());
    for (int index : uniqueOrder) {
        reordered.push_back(m_Layers[index]);
    }
    m_Layers = std::move(reordered);

    for (int i = 0; i < static_cast<int>(uniqueOrder.size()); ++i) {
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(uniqueOrder[i])) {
            node->layerIndex = i;
        }
    }
    RefreshGraphLayerMetadata();
}

void EditorModule::PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const std::string path = FileDialogs::OpenImageFileDialog("Add Image Node");
    if (!path.empty()) {
        AddImageNodeFromFile(path, graphPosition);
    }
}

bool EditorModule::AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    if (Raw::RawLoader::IsRawPath(path)) {
        return AddRawSourceNodeFromFile(path, graphPosition);
    }
    DecodedImageData decoded;
    if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
        return false;
    }

    return AddImageNodeFromPayload(BuildImagePayloadFromDecoded(path, std::move(decoded)), graphPosition);
}

bool EditorModule::AddRawSourceNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    Raw::RawImageData rawData;
    const bool loaded = Raw::RawLoader::LoadFile(path, rawData);
    if (!loaded && rawData.metadata.sourcePath.empty()) {
        rawData.metadata.sourcePath = path;
    }
    if (!loaded && rawData.metadata.error.empty()) {
        rawData.metadata.error = "Failed to load RAW file.";
    }

    EditorNodeGraph::RawSourcePayload payload = BuildRawPayloadFromMetadata(path, std::move(rawData.metadata));
    return AddRawSourceNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddImageNodeFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddImageNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
    }
    return node != nullptr;
}

bool EditorModule::AddRawSourceNodeFromPayload(EditorNodeGraph::RawSourcePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawSourceNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawNeuralDenoiseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawNeuralDenoisePayload payload;
    AddRawNeuralDenoiseNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawNeuralDenoiseNodeFromPayload(EditorNodeGraph::RawNeuralDenoisePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawNeuralDenoiseNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawDevelopNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    if (const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
            ApplyDevelopAutoSolve(payload, selected->rawSource.metadata, true);
        }
    }
    AddRawDevelopNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDevelopNodeFromPayload(EditorNodeGraph::RawDevelopPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDevelopNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::UpdateDevelopAutoState(
    int nodeId,
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    bool forceReanalysis,
    bool forceFullReanalysis) {
    if (payload.uiMode != EditorNodeGraph::RawDevelopUiMode::Auto) {
        m_DevelopAutoSolveTriggerHashes.erase(nodeId);
        m_DevelopAutoRawSolveTriggerHashes.erase(nodeId);
        m_DevelopAutoRawCalibrationHashes.erase(nodeId);
        return false;
    }

    const std::size_t triggerHash = BuildDevelopAutoSolveTriggerHash(payload, metadata);
    const std::size_t rawTriggerHash = BuildDevelopAutoRawSolveTriggerHash(payload, metadata);
    const auto it = m_DevelopAutoSolveTriggerHashes.find(nodeId);
    const auto rawIt = m_DevelopAutoRawSolveTriggerHashes.find(nodeId);
    const auto rawCalibrationIt = m_DevelopAutoRawCalibrationHashes.find(nodeId);
    const bool rawInputsChanged =
        rawIt == m_DevelopAutoRawSolveTriggerHashes.end() ||
        rawIt->second != rawTriggerHash;
    const bool explicitRawCalibrationNeeded =
        forceFullReanalysis &&
        (rawCalibrationIt == m_DevelopAutoRawCalibrationHashes.end() ||
         rawCalibrationIt->second != rawTriggerHash);
    const bool anySolveNeeded =
        forceReanalysis ||
        forceFullReanalysis ||
        it == m_DevelopAutoSolveTriggerHashes.end() ||
        it->second != triggerHash ||
        rawInputsChanged;
    if (!anySolveNeeded) {
        return false;
    }

    const bool fullSolveNeeded =
        forceFullReanalysis ||
        rawInputsChanged ||
        explicitRawCalibrationNeeded;
    ApplyDevelopAutoSolve(payload, metadata, true, fullSolveNeeded);
    m_DevelopAutoSolveTriggerHashes[nodeId] = BuildDevelopAutoSolveTriggerHash(payload, metadata);
    m_DevelopAutoRawSolveTriggerHashes[nodeId] = BuildDevelopAutoRawSolveTriggerHash(payload, metadata);
    if (fullSolveNeeded) {
        m_DevelopAutoRawCalibrationHashes[nodeId] = rawTriggerHash;
    }
    return true;
}

void EditorModule::AddRawDetailAutoMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDetailAutoMaskPayload payload;
    AddRawDetailAutoMaskNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDetailAutoMaskNodeFromPayload(EditorNodeGraph::RawDetailAutoMaskPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDetailAutoMaskNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawDetailFusionNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const bool selectedHasImageOutput = selected &&
        (selected->kind == EditorNodeGraph::NodeKind::Image ||
         selected->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         selected->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         selected->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         selected->kind == EditorNodeGraph::NodeKind::Layer ||
         selected->kind == EditorNodeGraph::NodeKind::Mix ||
         selected->kind == EditorNodeGraph::NodeKind::DataMath ||
         selected->kind == EditorNodeGraph::NodeKind::ImageGenerator ||
         selected->kind == EditorNodeGraph::NodeKind::ChannelCombine);
    const int upstreamNodeId = selectedHasImageOutput ? selected->id : -1;

    EditorNodeGraph::RawDetailFusionPayload fusionPayload;
    EditorNodeGraph::Node* fusionNode = m_NodeGraph.AddRawDetailFusionNode(std::move(fusionPayload), graphPosition);
    if (!fusionNode) {
        return;
    }
    const int fusionNodeId = fusionNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        ConnectGraphSockets(upstreamNodeId, EditorNodeGraph::kImageOutputSocketId, fusionNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    }
    SelectGraphNode(fusionNodeId);
    MarkRenderDirty(fusionNodeId);
}

bool EditorModule::AddRawDetailFusionNodeFromPayload(EditorNodeGraph::RawDetailFusionPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDetailFusionNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddHdrMergeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const bool selectedHasImageOutput = selected &&
        (selected->kind == EditorNodeGraph::NodeKind::Image ||
         selected->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         selected->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         selected->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         selected->kind == EditorNodeGraph::NodeKind::Layer ||
         selected->kind == EditorNodeGraph::NodeKind::Mix ||
         selected->kind == EditorNodeGraph::NodeKind::DataMath ||
         selected->kind == EditorNodeGraph::NodeKind::ImageGenerator ||
         selected->kind == EditorNodeGraph::NodeKind::ChannelCombine);
    const int upstreamNodeId = selectedHasImageOutput ? selected->id : -1;

    EditorNodeGraph::HdrMergePayload payload;
    EditorNodeGraph::Node* hdrNode = m_NodeGraph.AddHdrMergeNode(std::move(payload), graphPosition);
    if (!hdrNode) {
        return;
    }
    const int hdrNodeId = hdrNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(upstreamNodeId, EditorNodeGraph::kImageOutputSocketId, hdrNodeId, EditorNodeGraph::kHdrMergeInput1SocketId, &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "HDR Merge auto-connect failed: " + errorMessage,
                "hdr-merge-autoconnect");
        }
    }
    SelectGraphNode(hdrNodeId);
    MarkRenderDirty(hdrNodeId);
}

bool EditorModule::AddHdrMergeNodeFromPayload(EditorNodeGraph::HdrMergePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddHdrMergeNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::ConvertRawDetailFusionToHybrid(int fusionNodeId) {
    EditorNodeGraph::Node* fusionNode = m_NodeGraph.FindNode(fusionNodeId);
    if (!fusionNode || fusionNode->kind != EditorNodeGraph::NodeKind::RawDetailFusion) {
        return false;
    }
    const EditorNodeGraph::Link* maskInput = m_NodeGraph.FindInputLink(fusionNodeId, EditorNodeGraph::kMaskInputSocketId);
    if (!maskInput) {
        return false;
    }
    const EditorNodeGraph::Node* autoMaskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
    if (!autoMaskNode || autoMaskNode->kind != EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        maskInput->fromSocketId != EditorNodeGraph::kMaskOutputSocketId) {
        return false;
    }
    const int autoMaskNodeId = autoMaskNode->id;
    const EditorNodeGraph::Vec2 autoMaskPosition = autoMaskNode->position;
    const EditorNodeGraph::Vec2 fusionPosition = fusionNode->position;

    const EditorNodeGraph::Vec2 pos{
        (autoMaskPosition.x + fusionPosition.x) * 0.5f,
        autoMaskPosition.y
    };
    EditorNodeGraph::Node* levelsNode = m_NodeGraph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Levels, pos);
    if (!levelsNode) {
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphSockets(autoMaskNodeId, EditorNodeGraph::kMaskOutputSocketId, levelsNode->id, EditorNodeGraph::kMaskUtilityInputSocketId, &errorMessage)) {
        return false;
    }
    if (!ConnectGraphSockets(levelsNode->id, EditorNodeGraph::kMaskOutputSocketId, fusionNodeId, EditorNodeGraph::kMaskInputSocketId, &errorMessage)) {
        return false;
    }
    SelectGraphNode(levelsNode->id);
    MarkRenderDirty(fusionNodeId);
    return true;
}

bool EditorModule::AddFullRawTreeToSource(int rawSourceNodeId) {
    const EditorNodeGraph::Node* rawSourceNode = m_NodeGraph.FindNode(rawSourceNodeId);
    if (!rawSourceNode || rawSourceNode->kind != EditorNodeGraph::NodeKind::RawSource) {
        return false;
    }

    const int completedBefore = GetCompletedChainCount();
    const EditorNodeGraph::Vec2 sourcePosition = rawSourceNode->position;
    SelectGraphNode(rawSourceNodeId);

    constexpr float kNodeSpacing = 280.0f;
    AddRawDevelopNodeAt(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 1.0f, sourcePosition.y });
    const int developNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::ViewTransform, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 2.0f, sourcePosition.y });
    const int viewTransformNodeId = m_NodeGraph.GetSelectedNodeId();
    AddOutputNodeAt(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 3.0f, sourcePosition.y });
    const int outputNodeId = m_NodeGraph.GetSelectedNodeId();

    if (developNodeId <= 0 || viewTransformNodeId <= 0 || outputNodeId <= 0) {
        return false;
    }

    std::string errorMessage;
    const bool ok =
        ConnectGraphSockets(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, developNodeId, EditorNodeGraph::kRawInputSocketId, &errorMessage) &&
        ConnectGraphSockets(developNodeId, EditorNodeGraph::kImageOutputSocketId, viewTransformNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(viewTransformNodeId, EditorNodeGraph::kImageOutputSocketId, outputNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    if (!ok) {
        return false;
    }

    if (completedBefore < 2 && GetCompletedChainCount() >= 2) {
        EnsureCompositeNode();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MoveCompositeOutputToFront(outputNodeId);
    m_CompositeSelectedOutputNodeId = outputNodeId;
    m_NodeGraph.SetOutputNodeId(outputNodeId);
    MarkRenderDirty(outputNodeId);
    SelectGraphNode(outputNodeId);
    return true;
}

bool EditorModule::SplitLayerNodeIntoChannels(int layerNodeId) {
    EditorNodeGraph::Node* layerNode = m_NodeGraph.FindNode(layerNodeId);
    if (!layerNode || layerNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }
    if (layerNode->layerIndex < 0 || layerNode->layerIndex >= static_cast<int>(m_Layers.size())) {
        return false;
    }

    const EditorNodeGraph::Link* imageInput = m_NodeGraph.FindInputLink(layerNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!imageInput) {
        return false;
    }

    std::vector<EditorNodeGraph::Link> outputLinks;
    std::vector<EditorNodeGraph::Link> maskLinks;
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == layerNodeId && link.fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
            outputLinks.push_back(link);
        } else if (link.toNodeId == layerNodeId && link.toSocketId == EditorNodeGraph::kMaskInputSocketId) {
            maskLinks.push_back(link);
        }
    }
    if (outputLinks.empty()) {
        return false;
    }

    const EditorNodeGraph::Vec2 originalPos = layerNode->position;
    const int originalLayerIndex = layerNode->layerIndex;
    const LayerType originalLayerType = layerNode->layerType;
    const std::string originalTypeId = layerNode->typeId;
    const std::shared_ptr<LayerBase> originalLayer = m_Layers[originalLayerIndex];
    if (!originalLayer) {
        return false;
    }
    const EditorNodeGraph::Graph graphSnapshot = m_NodeGraph;
    const std::vector<std::shared_ptr<LayerBase>> layersSnapshot = m_Layers;

    std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(layerNodeId);
    downstreamNodeIds.erase(
        std::remove(downstreamNodeIds.begin(), downstreamNodeIds.end(), layerNodeId),
        downstreamNodeIds.end());

    std::vector<std::pair<int, EditorNodeGraph::Vec2>> originalDownstreamPositions;
    originalDownstreamPositions.reserve(downstreamNodeIds.size());
    for (const int downstreamNodeId : downstreamNodeIds) {
        if (const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(downstreamNodeId)) {
            originalDownstreamPositions.emplace_back(downstreamNodeId, downstream->position);
        }
    }

    std::array<std::shared_ptr<LayerBase>, 4> cloneLayers;
    for (std::shared_ptr<LayerBase>& cloneLayer : cloneLayers) {
        cloneLayer = CloneLayerInstance(originalLayer);
        if (!cloneLayer) {
            return false;
        }
    }

    for (const auto& entry : originalDownstreamPositions) {
        if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
            downstream->position.x = entry.second.x + 620.0f;
        }
    }

    AddChannelSplitNodeAt(EditorNodeGraph::Vec2{ originalPos.x - 250.0f, originalPos.y });
    const int splitNodeId = m_NodeGraph.GetSelectedNodeId();
    AddChannelCombineNodeAt(EditorNodeGraph::Vec2{ originalPos.x + 370.0f, originalPos.y });
    const int combineNodeId = m_NodeGraph.GetSelectedNodeId();
    if (splitNodeId <= 0 || combineNodeId <= 0) {
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        if (splitNodeId > 0) {
            RemoveGraphNode(splitNodeId);
        }
        if (combineNodeId > 0) {
            RemoveGraphNode(combineNodeId);
        }
        return false;
    }

    std::array<int, 4> cloneNodeIds{ -1, -1, -1, -1 };
    constexpr const char* kChannels[4] = { "r", "g", "b", "a" };
    constexpr float kRowOffsets[4] = { -240.0f, -80.0f, 80.0f, 240.0f };
    bool createdAllClones = true;
    for (int i = 0; i < 4; ++i) {
        const int newLayerIndex = static_cast<int>(m_Layers.size());
        m_Layers.push_back(cloneLayers[i]);
        EditorNodeGraph::Node* cloneNode = m_NodeGraph.AddLayerNode(
            originalLayerType,
            newLayerIndex,
            EditorNodeGraph::Vec2{ originalPos.x + 60.0f, originalPos.y + kRowOffsets[i] });
        if (!cloneNode) {
            createdAllClones = false;
            break;
        }
        cloneNode->typeId = originalTypeId;
        cloneNodeIds[i] = cloneNode->id;
    }
    if (!createdAllClones) {
        while (static_cast<int>(m_Layers.size()) > originalLayerIndex + 1) {
            m_Layers.pop_back();
        }
        for (int cloneNodeId : cloneNodeIds) {
            if (cloneNodeId > 0) {
                RemoveGraphNode(cloneNodeId);
            }
        }
        RemoveGraphNode(splitNodeId);
        RemoveGraphNode(combineNodeId);
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        RefreshGraphLayerMetadata();
        MarkRenderDirty();
        return false;
    }

    std::string errorMessage;
    bool ok = ConnectGraphSockets(imageInput->fromNodeId, imageInput->fromSocketId, splitNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    for (int i = 0; ok && i < 4; ++i) {
        ok = ConnectGraphSockets(splitNodeId, kChannels[i], cloneNodeIds[i], EditorNodeGraph::kImageInputSocketId, &errorMessage);
        if (ok) {
            ok = ConnectGraphSockets(cloneNodeIds[i], EditorNodeGraph::kImageOutputSocketId, combineNodeId, kChannels[i], &errorMessage);
        }
    }

    for (const EditorNodeGraph::Link& maskLink : maskLinks) {
        for (int cloneNodeId : cloneNodeIds) {
            if (!ok) {
                break;
            }
            ok = ConnectGraphSockets(maskLink.fromNodeId, maskLink.fromSocketId, cloneNodeId, maskLink.toSocketId, &errorMessage);
        }
        if (!ok) {
            break;
        }
    }

    for (const EditorNodeGraph::Link& outputLink : outputLinks) {
        if (!ok) {
            break;
        }
        if (outputLink.toNodeId == combineNodeId) {
            continue;
        }
        ok = ConnectGraphSockets(combineNodeId, EditorNodeGraph::kImageOutputSocketId, outputLink.toNodeId, outputLink.toSocketId, &errorMessage);
    }

    if (!ok) {
        m_NodeGraph = graphSnapshot;
        m_Layers = layersSnapshot;
        RefreshGraphLayerMetadata();
        MarkRenderDirty();
        return false;
    }

    ClearGraphAutoFocusIfTrackedNode(layerNodeId);
    if (m_CanvasToolOwnerNodeId == layerNodeId) {
        CancelCanvasTool();
    }
    RemoveLayer(originalLayerIndex);
    RefreshGraphLayerMetadata();
    SelectGraphNode(combineNodeId);
    MarkRenderDirty(combineNodeId);
    return true;
}

bool EditorModule::ToggleOutputNodeEnabled(int outputNodeId) {
    EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(outputNodeId);
    if (!outputNode || outputNode->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }

    const bool enabled = !outputNode->outputEnabled;
    if (!m_NodeGraph.SetOutputNodeEnabled(outputNodeId, enabled)) {
        return false;
    }

    outputNode = m_NodeGraph.FindNode(outputNodeId);
    if (!outputNode) {
        return false;
    }
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*outputNode);

    if (!outputNode->outputEnabled && m_CompositeSelectedOutputNodeId == outputNodeId) {
        int replacementOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
        if (replacementOutputNodeId == outputNodeId) {
            replacementOutputNodeId = -1;
        }
        m_CompositeSelectedOutputNodeId = replacementOutputNodeId;
    }
    if (!outputNode->outputEnabled) {
        if (CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId)) {
            if (item->texture != 0) {
                glDeleteTextures(1, &item->texture);
                item->texture = 0;
            }
        }
        m_CompositeSceneItems.erase(
            std::remove_if(
                m_CompositeSceneItems.begin(),
                m_CompositeSceneItems.end(),
                [outputNodeId](const CompositeSceneItem& item) { return item.outputNodeId == outputNodeId; }),
            m_CompositeSceneItems.end());
        m_CompositeZOrder.erase(
            std::remove(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), outputNodeId),
            m_CompositeZOrder.end());
        m_CompositeOutputDirtyGenerations.erase(outputNodeId);
        m_CompositeOutputRequestedGenerations.erase(outputNodeId);
        m_CompositeOutputCompletedGenerations.erase(outputNodeId);
    }
    if (!m_NodeGraph.IsOutputConnected()) {
        m_Pipeline.ClearOutput();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MarkRenderDirty(outputNodeId);
    return true;
}

bool EditorModule::RequestGraphImageChainImports(
    const std::vector<std::string>& paths,
    EditorNodeGraph::Vec2 sourcePosition) {
    std::vector<std::string> validPaths;
    validPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        if (!path.empty()) {
            validPaths.push_back(path);
        }
    }
    if (validPaths.empty()) {
        return false;
    }
    if (Async::IsBusy(m_GraphDropImportTaskState)) {
        m_PendingGraphDropImports.push_back(PendingGraphDropImportRequest{ std::move(validPaths), sourcePosition });
        return true;
    }

    return StartGraphImageChainImport(std::move(validPaths), sourcePosition);
}

bool EditorModule::StartGraphImageChainImport(
    std::vector<std::string> validPaths,
    EditorNodeGraph::Vec2 sourcePosition) {
    if (validPaths.empty()) {
        return false;
    }

    ++m_GraphDropImportGeneration;
    const std::uint64_t generation = m_GraphDropImportGeneration;
    m_GraphDropImportTaskState = Async::TaskState::Queued;
    m_GraphDropImportStatusText = validPaths.size() > 1
        ? "Loading dropped images into the graph..."
        : "Loading dropped image into the graph...";

    Async::TaskSystem::Get().Submit([this, generation, validPaths = std::move(validPaths), sourcePosition]() mutable {
        struct DecodedDropImage {
            std::string path;
            DecodedImageData decoded;
        };

        std::vector<DecodedDropImage> decodedImages;
        decodedImages.reserve(validPaths.size());
        std::vector<std::string> rawPaths;
        for (const std::string& path : validPaths) {
            if (Raw::RawLoader::IsRawPath(path)) {
                rawPaths.push_back(path);
                continue;
            }
            DecodedImageData decoded;
            if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
                continue;
            }
            decodedImages.push_back(DecodedDropImage{ path, std::move(decoded) });
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            sourcePosition,
            requestedCount = validPaths.size(),
            decodedImages = std::move(decodedImages),
            rawPaths = std::move(rawPaths)
        ]() mutable {
            if (generation != m_GraphDropImportGeneration) {
                return;
            }

            if (decodedImages.empty() && rawPaths.empty()) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to import the dropped images.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to import the dropped images.", "editor-graph-drop-import");
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Applying;
            m_GraphDropImportStatusText = "Creating image nodes...";

            constexpr float kGraphDropRowSpacing = 190.0f;
            const std::size_t totalNodes = decodedImages.size() + rawPaths.size();
            const float startY = sourcePosition.y - (static_cast<float>(totalNodes - 1) * kGraphDropRowSpacing * 0.5f);
            int importedCount = 0;
            size_t outputIndex = 0;
            for (size_t index = 0; index < decodedImages.size(); ++index, ++outputIndex) {
                EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                nodePosition.y = startY + static_cast<float>(outputIndex) * kGraphDropRowSpacing;
                if (AddGraphImageChainFromPayload(
                    BuildImagePayloadFromDecoded(decodedImages[index].path, std::move(decodedImages[index].decoded)),
                    nodePosition)) {
                    ++importedCount;
                }
            }
            for (const std::string& path : rawPaths) {
                EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                nodePosition.y = startY + static_cast<float>(outputIndex++) * kGraphDropRowSpacing;
                if (AddGraphRawChainFromFile(path, nodePosition)) {
                    ++importedCount;
                }
            }

            if (importedCount <= 0) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to create graph nodes for the dropped images.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to create graph nodes for the dropped images.", "editor-graph-drop-import");
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Idle;
            if (importedCount == static_cast<int>(requestedCount)) {
                m_GraphDropImportStatusText = importedCount == 1
                    ? "Imported 1 image into the graph."
                    : "Imported " + std::to_string(importedCount) + " images into the graph.";
            } else {
                m_GraphDropImportStatusText =
                    "Imported " + std::to_string(importedCount) + " of " + std::to_string(requestedCount) + " dropped images.";
            }
            QueueUiNotification(UiNotificationSeverity::Success, m_GraphDropImportStatusText, "editor-graph-drop-import");

            if (!m_PendingGraphDropImports.empty()) {
                PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
            }
        });
    });

    return true;
}

bool EditorModule::ConnectGraphImageNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }
    if (node->kind == EditorNodeGraph::NodeKind::Image) {
        if (node->image.pixels.empty()) {
            return false;
        }
        LoadSourceFromPixels(node->image.pixels.data(), node->image.width, node->image.height, node->image.channels);
    } else if (node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    m_NodeGraph.ConnectImageToOutput(nodeId);
    SelectGraphNode(nodeId);
    MarkRenderDirty();
    return true;
}

bool EditorModule::ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    const std::string fromSocket = from ? m_NodeGraph.DefaultOutputSocket(*from) : std::string();
    const EditorNodeGraph::Node* pendingTo = m_NodeGraph.FindNode(toNodeId);
    const std::string toSocket = pendingTo ? m_NodeGraph.DefaultInputSocket(*pendingTo) : std::string();
    return ConnectGraphSockets(fromNodeId, fromSocket, toNodeId, toSocket, errorMessage);
}

int EditorModule::FindDirectDownstreamToneCurveNode(int sourceNodeId) const {
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            m_NodeGraph.GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(link.toNodeId);
        if (downstream &&
            downstream->kind == EditorNodeGraph::NodeKind::Layer &&
            downstream->layerType == LayerType::ToneCurve) {
            return downstream->id;
        }
    }
    return -1;
}

int EditorModule::FindNearestDownstreamToneCurveNode(int sourceNodeId) const {
    int currentNodeId = sourceNodeId;
    const std::size_t maxHops = m_NodeGraph.GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        currentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(currentNodeId, 1);
        if (currentNodeId <= 0) {
            return -1;
        }
        const EditorNodeGraph::Node* currentNode = m_NodeGraph.FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::Layer &&
            currentNode->layerType == LayerType::ToneCurve) {
            return currentNode->id;
        }
    }
    return -1;
}

int EditorModule::FindNearestUpstreamRawDevelopNode(int sourceNodeId) const {
    int currentNodeId = sourceNodeId;
    const std::size_t maxHops = m_NodeGraph.GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        const EditorNodeGraph::Node* currentNode = m_NodeGraph.FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            return currentNode->id;
        }
        currentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(currentNodeId, -1);
    }
    return -1;
}

bool EditorModule::RawDevelopNodeUsesIntegratedTone(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    return node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled;
}

bool EditorModule::CanAbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId, std::string* reason) const {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        if (reason) {
            *reason = "No Develop node was found for this merge action.";
        }
        return false;
    }

    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);
    if (directToneNodeId <= 0) {
        if (reason) {
            *reason = "No direct downstream Tone Curve is connected to this Develop node.";
        }
        return false;
    }

    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        if (reason) {
            *reason = "The direct downstream node is not a Tone Curve layer.";
        }
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        m_NodeGraph.FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        m_NodeGraph.FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);
    if (toneMaskLink && developMaskLink &&
        (toneMaskLink->fromNodeId != developMaskLink->fromNodeId ||
         toneMaskLink->fromSocketId != developMaskLink->fromSocketId)) {
        if (reason) {
            *reason = "Develop already has a different finish mask connected, so this legacy Tone Curve cannot be absorbed automatically.";
        }
        return false;
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

bool EditorModule::SelectOrCreateToneFinishAfterNode(int sourceNodeId) {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    if (const int existingToneNodeId = FindNearestDownstreamToneCurveNode(sourceNodeId);
        existingToneNodeId > 0) {
        SelectGraphNode(existingToneNodeId);
        return true;
    }

    std::vector<EditorNodeGraph::Link> downstreamLinks;
    EditorNodeGraph::Vec2 tonePosition{ sourceNode->position.x + 280.0f, sourceNode->position.y };
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            m_NodeGraph.GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        downstreamLinks.push_back(link);
    }

    if (!downstreamLinks.empty()) {
        if (const EditorNodeGraph::Node* firstDownstream = m_NodeGraph.FindNode(downstreamLinks.front().toNodeId)) {
            tonePosition.x = (sourceNode->position.x + firstDownstream->position.x) * 0.5f;
            tonePosition.y = (sourceNode->position.y + firstDownstream->position.y) * 0.5f;
        }
    }

    AddLayerNodeAt(LayerType::ToneCurve, tonePosition);
    const int toneNodeId = m_NodeGraph.GetSelectedNodeId();
    if (toneNodeId <= 0) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Could not create a downstream Tone Curve node.",
            "raw-develop-tone-finish-create");
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphSockets(
            sourceNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            toneNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &errorMessage)) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            errorMessage.empty() ? "Could not connect Develop to the new Tone Curve node." : errorMessage,
            "raw-develop-tone-finish-connect");
        return false;
    }

    for (const EditorNodeGraph::Link& downstreamLink : downstreamLinks) {
        if (!ConnectGraphSockets(
                toneNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                downstreamLink.toNodeId,
                downstreamLink.toSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Could not reconnect one of the downstream finish-tone links." : errorMessage,
                "raw-develop-tone-finish-rewire");
            SelectGraphNode(toneNodeId);
            return false;
        }
    }

    SelectGraphNode(toneNodeId);
    return true;
}

bool EditorModule::AbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId) {
    EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    std::string absorbReason;
    if (!CanAbsorbDirectDownstreamToneFinishIntoDevelop(sourceNodeId, &absorbReason)) {
        if (!absorbReason.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                absorbReason,
                "raw-develop-tone-finish-absorb-unsafe");
        }
        return false;
    }
    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);

    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerIndex < 0 ||
        toneNode->layerIndex >= static_cast<int>(m_Layers.size()) ||
        !m_Layers[toneNode->layerIndex]) {
        return false;
    }

    ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[toneNode->layerIndex].get());
    if (!toneCurve) {
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        m_NodeGraph.FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        m_NodeGraph.FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);

    sourceNode->rawDevelop.integratedToneEnabled = true;
    sourceNode->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();

    if (toneMaskLink && !developMaskLink) {
        std::string errorMessage;
        if (!ConnectGraphSockets(
                toneMaskLink->fromNodeId,
                toneMaskLink->fromSocketId,
                sourceNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty()
                    ? "Could not transfer the legacy Tone Curve finish mask into Develop."
                    : errorMessage,
                "raw-develop-tone-finish-mask-transfer");
            return false;
        }
    }

    if (!RemoveGraphNode(directToneNodeId)) {
        return false;
    }

    SelectGraphNode(sourceNodeId);
    MarkRenderDirty(sourceNodeId);
    return true;
}

bool EditorModule::SelectUpstreamDevelopForToneNode(int toneNodeId) {
    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(toneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        return false;
    }

    const int rawDevelopNodeId = FindNearestUpstreamRawDevelopNode(toneNodeId);
    if (rawDevelopNodeId <= 0) {
        return false;
    }

    SelectGraphNode(rawDevelopNodeId);
    return true;
}

bool EditorModule::ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    const EditorNodeGraph::Node* targetNode = m_NodeGraph.FindNode(toNodeId);
    if (targetNode &&
        targetNode->kind == EditorNodeGraph::NodeKind::Output &&
        toSocketId == EditorNodeGraph::kImageInputSocketId &&
        fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
        std::unordered_set<int> visiting;
        const ScenePathState scenePath = AnalyzeScenePathFromNode(m_NodeGraph, m_Layers, fromNodeId, visiting);
        if (scenePath.sceneReferred && !scenePath.hasViewTransform) {
            const EditorNodeGraph::Vec2 fromPosition = from ? from->position : EditorNodeGraph::Vec2{};
            const EditorNodeGraph::Vec2 toPosition = targetNode->position;
            const EditorNodeGraph::Vec2 viewPosition{
                (fromPosition.x + toPosition.x) * 0.5f,
                (fromPosition.y + toPosition.y) * 0.5f
            };
            AddLayerNodeAt(LayerType::ViewTransform, viewPosition);
            const int viewNodeId = m_NodeGraph.GetSelectedNodeId();
            if (viewNodeId <= 0) {
                if (errorMessage) *errorMessage = "Could not create View Transform node.";
                return false;
            }
            if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, viewNodeId, EditorNodeGraph::kImageInputSocketId, errorMessage)) {
                return false;
            }
            if (!m_NodeGraph.TryConnectSockets(viewNodeId, EditorNodeGraph::kImageOutputSocketId, toNodeId, toSocketId, errorMessage)) {
                return false;
            }
            ApplyGraphLayerOrder();
            MarkRenderDirty();
            SelectGraphNode(viewNodeId);
            return true;
        }
    }

    if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, errorMessage)) {
        return false;
    }

    if (from && from->kind == EditorNodeGraph::NodeKind::Image &&
        ConnectionUsesImageAsRenderSource(m_NodeGraph, fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        LoadSourceFromPixels(from->image.pixels.data(), from->image.width, from->image.height, from->image.channels);
    }

    ApplyGraphLayerOrder();
    MarkRenderDirty();
    const EditorNodeGraph::Node* to = m_NodeGraph.FindNode(toNodeId);
    if (to && to->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectGraphNode(toNodeId);
    } else if (from) {
        SelectGraphNode(fromNodeId);
    }
    return true;
}

bool EditorModule::OutputPathNeedsViewTransform(int outputNodeId) const {
    const EditorNodeGraph::Node* output = m_NodeGraph.FindNode(outputNodeId);
    if (!output || output->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }
    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }
    std::unordered_set<int> visiting;
    const ScenePathState scenePath = AnalyzeScenePathFromNode(m_NodeGraph, m_Layers, input->fromNodeId, visiting);
    return scenePath.sceneReferred && !scenePath.hasViewTransform;
}

bool EditorModule::SelectedLayerInputContainsViewTransform() const {
    if (m_SelectedLayerIndex < 0) {
        return false;
    }
    const EditorNodeGraph::Node* selectedNode = m_NodeGraph.FindNodeByLayerIndex(m_SelectedLayerIndex);
    if (!selectedNode || selectedNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }

    std::unordered_set<int> visiting;
    std::function<bool(int)> inputContainsViewTransform = [&](int nodeId) -> bool {
        if (!visiting.insert(nodeId).second) {
            return false;
        }
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            visiting.erase(nodeId);
            return false;
        }
        if (node->kind == EditorNodeGraph::NodeKind::Layer && node->layerType == LayerType::ViewTransform) {
            visiting.erase(nodeId);
            return true;
        }
        auto checkInput = [&](const std::string& socketId) {
            const EditorNodeGraph::Link* link = m_NodeGraph.FindInputLink(nodeId, socketId);
            return link ? inputContainsViewTransform(link->fromNodeId) : false;
        };
        bool found = false;
        if (node->kind == EditorNodeGraph::NodeKind::Mix ||
            node->kind == EditorNodeGraph::NodeKind::DataMath) {
            found = checkInput(EditorNodeGraph::kMixInputASocketId) ||
                checkInput(EditorNodeGraph::kMixInputBSocketId);
        } else if (node->kind == EditorNodeGraph::NodeKind::HdrMerge) {
            found = checkInput(EditorNodeGraph::kHdrMergeInput1SocketId) ||
                checkInput(EditorNodeGraph::kHdrMergeInput2SocketId) ||
                checkInput(EditorNodeGraph::kHdrMergeInput3SocketId);
        } else if (node->kind == EditorNodeGraph::NodeKind::ChannelCombine) {
            found = checkInput("r") || checkInput("g") || checkInput("b") || checkInput("a");
        } else if (node->kind != EditorNodeGraph::NodeKind::RawSource &&
            node->kind != EditorNodeGraph::NodeKind::Image) {
            found = checkInput(EditorNodeGraph::kImageInputSocketId);
        }
        visiting.erase(nodeId);
        return found;
    };

    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(selectedNode->id, EditorNodeGraph::kImageInputSocketId);
    return input ? inputContainsViewTransform(input->fromNodeId) : false;
}

bool EditorModule::RenderLayerControlsWithDirtyTracking(
    EditorNodeGraph::Node& node,
    const std::function<void(LayerBase&)>& renderControls) {
    if (node.kind != EditorNodeGraph::NodeKind::Layer ||
        node.layerIndex < 0 ||
        node.layerIndex >= static_cast<int>(m_Layers.size()) ||
        !m_Layers[node.layerIndex]) {
        return false;
    }

    LayerBase& layer = *m_Layers[node.layerIndex];
    const nlohmann::json before = layer.Serialize();
    const bool beforeEnabled = layer.IsEnabled();
    const bool beforeVisible = layer.IsVisible();
    renderControls(layer);
    const nlohmann::json after = layer.Serialize();
    if (before != after ||
        beforeEnabled != layer.IsEnabled() ||
        beforeVisible != layer.IsVisible()) {
        MarkRenderDirty(node.id);
        return true;
    }
    return false;
}

void EditorModule::MarkSelectedLayerRenderDirty() {
    if (m_SelectedLayerIndex >= 0) {
        if (const EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(m_SelectedLayerIndex)) {
            MarkRenderDirty(node->id);
            return;
        }
    }
    MarkRenderDirty();
}

bool EditorModule::RemoveGraphLink(int fromNodeId, int toNodeId) {
    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, toNodeId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphLink() {
    const bool removed = m_NodeGraph.RemoveSelectedLink();
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::RemoveGraphNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }

    const std::vector<GraphReconnectPlan> reconnectPlans =
        BuildReconnectPlansForNodeRemoval(m_NodeGraph, *node);

    ClearGraphAutoFocusIfTrackedNode(nodeId);
    if (m_CanvasToolOwnerNodeId == nodeId) {
        CancelCanvasTool();
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer) {
        const int layerIndex = node->layerIndex;
        RemoveLayer(node->layerIndex);
        for (const GraphReconnectPlan& plan : reconnectPlans) {
            std::string errorMessage;
            ConnectGraphSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &errorMessage);
        }
        if (layerIndex >= 0) {
            RefreshGraphLayerMetadata();
        }
        return true;
    }

    const bool removed = m_NodeGraph.RemoveNode(nodeId);
    if (removed && !m_NodeGraph.IsOutputConnected()) {
        m_Pipeline.ClearOutput();
    }
    if (removed) {
        for (const GraphReconnectPlan& plan : reconnectPlans) {
            std::string errorMessage;
            ConnectGraphSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &errorMessage);
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphNodes() {
    std::vector<int> nodeIds = m_NodeGraph.GetSelectedNodeIds();
    if (nodeIds.empty()) {
        return false;
    }

    std::sort(nodeIds.begin(), nodeIds.end(), [this](int a, int b) {
        const EditorNodeGraph::Node* nodeA = m_NodeGraph.FindNode(a);
        const EditorNodeGraph::Node* nodeB = m_NodeGraph.FindNode(b);
        const int layerA = nodeA && nodeA->kind == EditorNodeGraph::NodeKind::Layer ? nodeA->layerIndex : -1;
        const int layerB = nodeB && nodeB->kind == EditorNodeGraph::NodeKind::Layer ? nodeB->layerIndex : -1;
        return layerA > layerB;
    });

    bool removedAny = false;
    for (int nodeId : nodeIds) {
        removedAny = RemoveGraphNode(nodeId) || removedAny;
    }
    m_NodeGraph.ClearSelection();
    RefreshGraphLayerMetadata();
    if (removedAny) {
        MarkRenderDirty();
    }
    return removedAny;
}

void EditorModule::AddScopeNodeAt(EditorNodeGraph::ScopeKind scopeKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddScopeNode(scopeKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskGeneratorNode(maskKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode combineMode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskCombineNode(combineMode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskUtilityNode(utilityKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddCustomMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::CustomMaskPayload payload;
    if (m_Pipeline.GetCanvasWidth() > 0 && m_Pipeline.GetCanvasHeight() > 0) {
        payload.width = std::clamp(m_Pipeline.GetCanvasWidth(), 1, 4096);
        payload.height = std::clamp(m_Pipeline.GetCanvasHeight(), 1, 4096);
    }
    payload.rasterLayer.assign(
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height),
        0.0f);

    if (EditorNodeGraph::Node* node = m_NodeGraph.AddCustomMaskNode(std::move(payload), graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
        SwitchToComplexNodeSubWindow(node->id);
    }
}

void EditorModule::AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageToMaskNode(converterKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

bool EditorModule::CreateToneCurveSelectionMask(
    int toneCurveNodeId,
    float low,
    float high,
    float softness,
    const std::array<float, 4>& sampleRgba,
    float sampleLuma,
    float sampleU,
    float sampleV,
    float toneSimilarity,
    float colorSimilarity,
    float regionRadius,
    float regionFeather,
    float edgeSensitivity,
    float localCoherence,
    ToneCurveScopeMaskAction action) {
    EditorNodeGraph::Node* toneCurveNode = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!toneCurveNode) {
        return false;
    }

    int maskOwnerNodeId = toneCurveNodeId;
    int sourceImageNodeId = -1;
    std::string sourceImageSocketId;
    const EditorNodeGraph::Link* maskInput = nullptr;
    if (toneCurveNode->kind == EditorNodeGraph::NodeKind::Layer &&
        toneCurveNode->layerType == LayerType::ToneCurve) {
        const EditorNodeGraph::Link* imageInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!imageInput) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Tone Curve needs an image input before a tone scope mask can be created.",
                "tone-curve-mask-create");
            return false;
        }
        sourceImageNodeId = imageInput->fromNodeId;
        sourceImageSocketId = imageInput->fromSocketId;
        maskInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kMaskInputSocketId);
    } else if (toneCurveNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        if (!toneCurveNode->rawDevelop.integratedToneEnabled) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Develop needs its finish stage enabled before creating a tone scope mask.",
                "tone-curve-mask-create");
            return false;
        }
        if (!m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kRawInputSocketId)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Develop needs a RAW input before a finish scope mask can be created.",
                "tone-curve-mask-create");
            return false;
        }
        sourceImageNodeId = toneCurveNodeId;
        sourceImageSocketId = EditorNodeGraph::kPreFinishImageOutputSocketId;
        maskInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kMaskInputSocketId);
    } else {
        return false;
    }

    EditorNodeGraph::Node* maskNode = nullptr;
    EditorNodeGraph::Node* combineNode = nullptr;
    const bool hadExistingMaskInput = maskInput != nullptr;
    const bool startNewScopedMask = action == ToneCurveScopeMaskAction::NewMask;
    const EditorNodeGraph::MaskCombineMode requestedCombineMode = ToGraphMaskCombineMode(action);
    bool reusedExistingToneScopeMask = false;
    if (maskInput) {
        combineNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
        if (combineNode && combineNode->kind == EditorNodeGraph::NodeKind::MaskCombine) {
            const EditorNodeGraph::Link* inputA = m_NodeGraph.FindInputLink(combineNode->id, EditorNodeGraph::kMaskCombineInputASocketId);
            const EditorNodeGraph::Link* inputB = m_NodeGraph.FindInputLink(combineNode->id, EditorNodeGraph::kMaskCombineInputBSocketId);
            for (const EditorNodeGraph::Link* input : { inputA, inputB }) {
                if (!input) {
                    continue;
                }
                EditorNodeGraph::Node* candidate = m_NodeGraph.FindNode(input->fromNodeId);
                if (candidate &&
                    candidate->kind == EditorNodeGraph::NodeKind::ImageToMask &&
                    candidate->title == "Tone Scope Mask") {
                    maskNode = candidate;
                    reusedExistingToneScopeMask = true;
                    break;
                }
            }
        } else {
            maskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
            if (!maskNode || maskNode->kind != EditorNodeGraph::NodeKind::ImageToMask || maskNode->title != "Tone Scope Mask") {
                maskNode = nullptr;
            } else {
                reusedExistingToneScopeMask = true;
            }
        }
    } else {
        const EditorNodeGraph::Vec2 position{
            toneCurveNode->position.x - 250.0f,
            toneCurveNode->position.y + 135.0f
        };
        maskNode = m_NodeGraph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::Luminance, position);
        if (!maskNode) {
            return false;
        }
    }

    if (!maskNode) {
        const EditorNodeGraph::Vec2 position{
            toneCurveNode->position.x - 250.0f,
            toneCurveNode->position.y + 135.0f
        };
        maskNode = m_NodeGraph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::Luminance, position);
        if (!maskNode) {
            return false;
        }
        maskNode->title = "Tone Scope Mask";
    } else if (maskNode->title.empty()) {
        maskNode->title = "Tone Scope Mask";
    }

    const EditorNodeGraph::Link* maskImageInput = m_NodeGraph.FindInputLink(maskNode->id, EditorNodeGraph::kImageInputSocketId);
    if (!maskImageInput ||
        maskImageInput->fromNodeId != sourceImageNodeId ||
        maskImageInput->fromSocketId != sourceImageSocketId) {
        if (maskImageInput) {
            RemoveGraphLink(maskImageInput->fromNodeId, maskImageInput->fromSocketId, maskNode->id, EditorNodeGraph::kImageInputSocketId);
        }
        std::string errorMessage;
        if (!ConnectGraphSockets(
                sourceImageNodeId,
                sourceImageSocketId,
                maskNode->id,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to connect the tone scope mask to the finish-stage input." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    }

    if (startNewScopedMask && maskInput) {
        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);
        combineNode = nullptr;
        maskInput = nullptr;
    }

    if (!maskInput) {
        std::string errorMessage;
        if (!ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to connect the tone scope mask to the finish-stage target." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    } else if (!combineNode && maskInput->fromNodeId != maskNode->id) {
        const EditorNodeGraph::Node* existingMaskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
        const EditorNodeGraph::Vec2 combinePosition{
            toneCurveNode->position.x - 125.0f,
            toneCurveNode->position.y + 140.0f
        };
        combineNode = m_NodeGraph.AddMaskCombineNode(requestedCombineMode, combinePosition);
        if (!combineNode) {
            return false;
        }
        combineNode->title = "Tone Scope Combine";

        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);

        std::string errorMessage;
        if (!ConnectGraphSockets(
                existingMaskNode ? existingMaskNode->id : maskInput->fromNodeId,
                maskInput->fromSocketId,
                combineNode->id,
                EditorNodeGraph::kMaskCombineInputASocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                combineNode->id,
                EditorNodeGraph::kMaskCombineInputBSocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                combineNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to combine the existing mask with the new tone scope mask." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    } else if (combineNode &&
               (m_NodeGraph.HasLink(maskNode->id, EditorNodeGraph::kMaskOutputSocketId, combineNode->id, EditorNodeGraph::kMaskCombineInputASocketId) ||
                m_NodeGraph.HasLink(maskNode->id, EditorNodeGraph::kMaskOutputSocketId, combineNode->id, EditorNodeGraph::kMaskCombineInputBSocketId))) {
        combineNode->maskCombineMode = requestedCombineMode;
    } else if (maskInput && maskInput->fromNodeId != maskNode->id) {
        const EditorNodeGraph::Vec2 combinePosition{
            toneCurveNode->position.x - 125.0f,
            toneCurveNode->position.y + 140.0f
        };
        EditorNodeGraph::Node* nestedCombine = m_NodeGraph.AddMaskCombineNode(requestedCombineMode, combinePosition);
        if (!nestedCombine) {
            return false;
        }
        nestedCombine->title = "Tone Scope Combine";

        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);

        std::string errorMessage;
        if (!ConnectGraphSockets(
                maskInput->fromNodeId,
                maskInput->fromSocketId,
                nestedCombine->id,
                EditorNodeGraph::kMaskCombineInputASocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                nestedCombine->id,
                EditorNodeGraph::kMaskCombineInputBSocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                nestedCombine->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to refine the existing mask with a new tone scope component." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
        combineNode = nestedCombine;
    }

    const float clampedLow = std::clamp(std::min(low, high), 0.0f, 1.0f);
    const float clampedHigh = std::clamp(std::max(low, high), 0.0f, 1.0f);
    const float clampedSampleRgb[3] = {
        std::clamp(sampleRgba[0], 0.0f, 16.0f),
        std::clamp(sampleRgba[1], 0.0f, 16.0f),
        std::clamp(sampleRgba[2], 0.0f, 16.0f)
    };
    const float clampedSampleLuma = std::clamp(sampleLuma, 0.0f, 16.0f);

    maskNode->imageToMaskKind = EditorNodeGraph::ImageToMaskKind::SampledRange;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*maskNode);
    maskNode->title = "Tone Scope Mask";
    maskNode->imageToMaskSettings.low = clampedLow;
    maskNode->imageToMaskSettings.high = std::max(clampedLow + 0.0001f, clampedHigh);
    maskNode->imageToMaskSettings.softness = std::clamp(softness, 0.0f, 0.5f);
    maskNode->imageToMaskSettings.invert = false;
    maskNode->imageToMaskSettings.sampleU = std::clamp(sampleU, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.sampleV = std::clamp(sampleV, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.toneSimilarity = std::clamp(toneSimilarity, 0.02f, 0.35f);
    maskNode->imageToMaskSettings.colorSimilarity = std::clamp(colorSimilarity, 0.02f, 0.50f);
    maskNode->imageToMaskSettings.regionRadius = std::clamp(regionRadius, 0.05f, 1.0f);
    maskNode->imageToMaskSettings.regionFeather = std::clamp(regionFeather, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.edgeSensitivity = std::clamp(edgeSensitivity, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.localCoherence = std::clamp(localCoherence, 0.0f, 1.0f);

    auto clearExtraSamples = [&](EditorNodeGraph::ImageToMaskSettings& settings) {
        for (int i = 0; i < 4; ++i) {
            settings.extraSampleRgb[i][0] = 0.5f;
            settings.extraSampleRgb[i][1] = 0.5f;
            settings.extraSampleRgb[i][2] = 0.5f;
            settings.extraSampleLuma[i] = 0.5f;
        }
    };
    auto resetPrimarySample = [&](EditorNodeGraph::ImageToMaskSettings& settings) {
        settings.sampleCount = 1;
        settings.sampleRgb[0] = clampedSampleRgb[0];
        settings.sampleRgb[1] = clampedSampleRgb[1];
        settings.sampleRgb[2] = clampedSampleRgb[2];
        settings.sampleLuma = clampedSampleLuma;
        clearExtraSamples(settings);
    };
    auto sampleMatches = [&](const EditorNodeGraph::ImageToMaskSettings& settings, int sampleIndex) {
        if (sampleIndex <= 0) {
            return std::abs(settings.sampleRgb[0] - clampedSampleRgb[0]) < 0.0005f &&
                std::abs(settings.sampleRgb[1] - clampedSampleRgb[1]) < 0.0005f &&
                std::abs(settings.sampleRgb[2] - clampedSampleRgb[2]) < 0.0005f &&
                std::abs(settings.sampleLuma - clampedSampleLuma) < 0.0005f;
        }
        const int extraIndex = sampleIndex - 1;
        return std::abs(settings.extraSampleRgb[extraIndex][0] - clampedSampleRgb[0]) < 0.0005f &&
            std::abs(settings.extraSampleRgb[extraIndex][1] - clampedSampleRgb[1]) < 0.0005f &&
            std::abs(settings.extraSampleRgb[extraIndex][2] - clampedSampleRgb[2]) < 0.0005f &&
            std::abs(settings.extraSampleLuma[extraIndex] - clampedSampleLuma) < 0.0005f;
    };

    bool appendedSample = false;
    bool duplicateSample = false;
    bool sampleCapacityReached = false;
    const bool allowSampleAppend = !startNewScopedMask && reusedExistingToneScopeMask;
    EditorNodeGraph::ImageToMaskSettings& imageToMaskSettings = maskNode->imageToMaskSettings;
    if (imageToMaskSettings.sampleCount < 1 || imageToMaskSettings.sampleCount > 5) {
        imageToMaskSettings.sampleCount = 1;
    }
    if (allowSampleAppend && imageToMaskSettings.sampleCount >= 1) {
        for (int i = 0; i < imageToMaskSettings.sampleCount; ++i) {
            if (sampleMatches(imageToMaskSettings, i)) {
                duplicateSample = true;
                break;
            }
        }
        if (!duplicateSample) {
            if (imageToMaskSettings.sampleCount < 5) {
                const int extraIndex = imageToMaskSettings.sampleCount - 1;
                imageToMaskSettings.extraSampleRgb[extraIndex][0] = clampedSampleRgb[0];
                imageToMaskSettings.extraSampleRgb[extraIndex][1] = clampedSampleRgb[1];
                imageToMaskSettings.extraSampleRgb[extraIndex][2] = clampedSampleRgb[2];
                imageToMaskSettings.extraSampleLuma[extraIndex] = clampedSampleLuma;
                imageToMaskSettings.sampleCount += 1;
                appendedSample = true;
            } else {
                sampleCapacityReached = true;
            }
        }
    } else {
        resetPrimarySample(imageToMaskSettings);
    }

    if (!allowSampleAppend && !appendedSample) {
        resetPrimarySample(imageToMaskSettings);
    } else if (imageToMaskSettings.sampleCount <= 0) {
        resetPrimarySample(imageToMaskSettings);
    }
    SelectGraphNode(
        toneCurveNode->kind == EditorNodeGraph::NodeKind::RawDevelop
            ? maskOwnerNodeId
            : maskNode->id);
    MarkRenderDirty(maskOwnerNodeId);
    if (sampleCapacityReached) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Tone scope mask already has five samples. Refine settings were updated, but no additional sample was added.",
            "tone-curve-mask-create");
    } else if (duplicateSample) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "That sampled tone is already present in the tone scope mask. Refine settings were updated.",
            "tone-curve-mask-create");
    } else {
        QueueUiNotification(
            UiNotificationSeverity::Success,
            appendedSample
                ? ("Tone scope mask refined with sample " + std::to_string(imageToMaskSettings.sampleCount) + " of 5.")
                : (startNewScopedMask
                    ? "Created a new scoped tone mask from the sampled tone and color range."
                    : (!hadExistingMaskInput
                        ? "Created a scoped tone mask from the sampled tone and color range."
                        : (action == ToneCurveScopeMaskAction::Add
                        ? "Added a sampled tone scope component to the existing mask."
                        : (action == ToneCurveScopeMaskAction::Subtract
                            ? "Subtracted a sampled tone scope component from the existing mask."
                            : "Intersected the existing mask with a sampled tone scope component.")))),
            "tone-curve-mask-create");
    }
    return true;
}

void EditorModule::AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageGeneratorNode(generatorKind, graphPosition)) {
        const int nodeId = node->id;
        SelectGraphNode(nodeId);
        if (GetConnectedOutputCount() == 0) {
            if (EditorNodeGraph::Node* outputNode = m_NodeGraph.AddOutputNode(
                EditorNodeGraph::Vec2{ graphPosition.x + 330.0f, graphPosition.y })) {
                const int outputNodeId = outputNode->id;
                std::string errorMessage;
                ConnectGraphNodes(nodeId, outputNodeId, &errorMessage);
                if (GetCompletedChainCount() == 1 && m_Pipeline.GetSourcePixelsRaw().empty()) {
                    EnterSingleOutputPreviewMode();
                }
            }
        }
        MarkRenderDirty();
    }
}

void EditorModule::AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMixNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddDataMathNodeAt(EditorNodeGraph::DataMathMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddDataMathNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddPreviewNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddChannelSplitNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddChannelSplitNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddChannelCombineNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddChannelCombineNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddOutputNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty();
    }
}

void EditorModule::AutoLayoutGraph() {
    m_NodeGraph.AutoLayout();
}

void EditorModule::DisconnectGraphOutput() {
    m_NodeGraph.DisconnectOutput();
    m_Pipeline.ClearOutput();
    MarkRenderDirty();
}

void EditorModule::RefreshGraphLayerMetadata() {
    m_NodeGraph.SyncLayerNodes(static_cast<int>(m_Layers.size()));

    for (int i = 0; i < static_cast<int>(m_Layers.size()); ++i) {
        EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(i);
        if (!node) {
            continue;
        }

        const nlohmann::json layerJson = m_Layers[i]->Serialize();
        const std::string typeId = layerJson.value("type", std::string());
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(typeId);
        node->typeId = typeId;
        if (descriptor) {
            node->layerType = descriptor->type;
            node->title = descriptor->displayName;
        } else {
            node->title = m_Layers[i]->GetDefaultName();
        }
    }
}

std::vector<std::shared_ptr<LayerBase>> EditorModule::BuildGraphRenderLayers() const {
    std::vector<std::shared_ptr<LayerBase>> renderLayers;
    for (int index : m_NodeGraph.GetRenderLayerIndexPath()) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
            renderLayers.push_back(m_Layers[index]);
        }
    }
    return renderLayers;
}

std::vector<RenderLayerStep> EditorModule::BuildGraphRenderSteps() const {
    std::vector<RenderLayerStep> steps;
    for (int nodeId : m_NodeGraph.GetRenderLayerNodePath()) {
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node || node->kind != EditorNodeGraph::NodeKind::Layer ||
            node->layerIndex < 0 || node->layerIndex >= static_cast<int>(m_Layers.size())) {
            continue;
        }

        RenderLayerStep step;
        step.layer = m_Layers[node->layerIndex];
        if (const EditorNodeGraph::Link* maskLink = m_NodeGraph.FindAnyInputLink(node->id, EditorNodeGraph::kMaskInputSocketId)) {
            const EditorNodeGraph::Node* maskNode = m_NodeGraph.FindNode(maskLink->fromNodeId);
            if (maskNode && maskNode->kind == EditorNodeGraph::NodeKind::MaskGenerator) {
                step.maskNodeId = maskNode->id;
            }
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

std::vector<RenderMaskSource> EditorModule::BuildGraphRenderMasks() const {
    std::vector<int> usedMaskNodeIds;
    for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
        if (step.maskNodeId > 0 &&
            std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), step.maskNodeId) == usedMaskNodeIds.end()) {
            usedMaskNodeIds.push_back(step.maskNodeId);
        }
    }

    std::vector<RenderMaskSource> masks;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::MaskGenerator) {
            continue;
        }
        if (std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), node.id) == usedMaskNodeIds.end()) {
            continue;
        }

        RenderMaskSource mask;
        mask.nodeId = node.id;
        mask.kind = ToRenderMaskKind(node.maskKind);
        mask.settings = ToRenderMaskSettings(node.maskSettings);
        masks.push_back(mask);
    }
    return masks;
}

RenderGraphSnapshot EditorModule::BuildGraphSnapshot() const {
    RenderGraphSnapshot snapshot;
    snapshot.outputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        RenderGraphNode renderNode;
        renderNode.nodeId = node.id;
        renderNode.requestRevision = std::max<std::uint64_t>(1, GetNodeDirtyGeneration(node.id));
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::Image:
                renderNode.kind = RenderGraphNodeKind::Image;
                renderNode.image.pixels = node.image.pixels;
                renderNode.image.width = node.image.width;
                renderNode.image.height = node.image.height;
                renderNode.image.channels = node.image.channels;
                break;
            case EditorNodeGraph::NodeKind::RawSource:
                renderNode.kind = RenderGraphNodeKind::RawSource;
                renderNode.rawSource.sourcePath = node.rawSource.sourcePath;
                renderNode.rawSource.metadata = node.rawSource.metadata;
                break;
            case EditorNodeGraph::NodeKind::RawNeuralDenoise:
                renderNode.kind = RenderGraphNodeKind::RawNeuralDenoise;
                renderNode.rawNeuralDenoise.settings = node.rawNeuralDenoise.settings;
                break;
            case EditorNodeGraph::NodeKind::RawDevelop:
                renderNode.kind = RenderGraphNodeKind::RawDevelop;
                renderNode.rawDevelop.settings = node.rawDevelop.settings;
                renderNode.rawDevelop.scenePrepEnabled = true;
                renderNode.rawDevelop.scenePrepSettings = node.rawDevelop.scenePrepSettings;
                renderNode.rawDevelop.integratedToneEnabled = true;
                renderNode.rawDevelop.integratedToneLayerJson = node.rawDevelop.integratedToneLayerJson;
                break;
            case EditorNodeGraph::NodeKind::RawDetailAutoMask:
                renderNode.kind = RenderGraphNodeKind::RawDetailAutoMask;
                renderNode.rawDetailAutoMask.settings = node.rawDetailAutoMask.settings;
                break;
            case EditorNodeGraph::NodeKind::RawDetailFusion:
                renderNode.kind = RenderGraphNodeKind::RawDetailFusion;
                renderNode.rawDetailFusion.settings = node.rawDetailFusion.settings;
                break;
            case EditorNodeGraph::NodeKind::HdrMerge:
                renderNode.kind = RenderGraphNodeKind::HdrMerge;
                renderNode.hdrMerge.settings = node.hdrMerge.settings;
                break;
            case EditorNodeGraph::NodeKind::Layer:
                renderNode.kind = RenderGraphNodeKind::Layer;
                if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node.layerIndex]) {
                    renderNode.layerJson = m_Layers[node.layerIndex]->Serialize();
                }
                break;
            case EditorNodeGraph::NodeKind::Output:
                renderNode.kind = RenderGraphNodeKind::Output;
                break;
            case EditorNodeGraph::NodeKind::MaskGenerator:
                renderNode.kind = RenderGraphNodeKind::MaskGenerator;
                renderNode.maskKind = ToRenderMaskKind(node.maskKind);
                renderNode.maskSettings = ToRenderMaskSettings(node.maskSettings);
                break;
            case EditorNodeGraph::NodeKind::MaskCombine:
                renderNode.kind = RenderGraphNodeKind::MaskCombine;
                renderNode.maskCombineMode = ToRenderMaskCombineMode(node.maskCombineMode);
                break;
            case EditorNodeGraph::NodeKind::CustomMask:
                renderNode.kind = RenderGraphNodeKind::CustomMask;
                renderNode.customMask = ToRenderCustomMaskPayload(node.customMask);
                break;
            case EditorNodeGraph::NodeKind::MaskUtility:
                renderNode.kind = RenderGraphNodeKind::MaskUtility;
                renderNode.maskUtilityKind = ToRenderMaskUtilityKind(node.maskUtilityKind);
                renderNode.maskUtilitySettings = ToRenderMaskUtilitySettings(node.maskUtilitySettings);
                break;
            case EditorNodeGraph::NodeKind::ImageToMask:
                renderNode.kind = RenderGraphNodeKind::ImageToMask;
                renderNode.imageToMaskKind = ToRenderImageToMaskKind(node.imageToMaskKind);
                renderNode.imageToMaskSettings = ToRenderImageToMaskSettings(node.imageToMaskSettings);
                break;
            case EditorNodeGraph::NodeKind::ImageGenerator:
                renderNode.kind = RenderGraphNodeKind::ImageGenerator;
                renderNode.imageGeneratorKind = ToRenderImageGeneratorKind(node.imageGeneratorKind);
                renderNode.imageGeneratorSettings = ToRenderImageGeneratorSettings(node.imageGeneratorSettings);
                break;
            case EditorNodeGraph::NodeKind::Mix:
                renderNode.kind = RenderGraphNodeKind::Mix;
                renderNode.mixBlendMode = ToRenderMixBlendMode(node.mixBlendMode);
                renderNode.mixFactor = node.mixFactor;
                break;
            case EditorNodeGraph::NodeKind::DataMath:
                renderNode.kind = RenderGraphNodeKind::DataMath;
                renderNode.dataMathMode = static_cast<RenderDataMathMode>(node.dataMathMode);
                renderNode.dataMathSettings.constantA = node.dataMathSettings.constantA;
                renderNode.dataMathSettings.constantB = node.dataMathSettings.constantB;
                renderNode.dataMathSettings.minValue = node.dataMathSettings.minValue;
                renderNode.dataMathSettings.maxValue = node.dataMathSettings.maxValue;
                renderNode.dataMathSettings.outMin = node.dataMathSettings.outMin;
                renderNode.dataMathSettings.outMax = node.dataMathSettings.outMax;
                break;
            case EditorNodeGraph::NodeKind::ChannelSplit:
                renderNode.kind = RenderGraphNodeKind::ChannelSplit;
                break;
            case EditorNodeGraph::NodeKind::ChannelCombine:
                renderNode.kind = RenderGraphNodeKind::ChannelCombine;
                break;
            case EditorNodeGraph::NodeKind::Composite:
            case EditorNodeGraph::NodeKind::Scope:
            case EditorNodeGraph::NodeKind::Preview:
                continue;
        }
        snapshot.nodes.push_back(std::move(renderNode));
    }

    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (m_NodeGraph.GetLinkRole(link) == EditorNodeGraph::LinkRole::Scope) {
            continue;
        }
        snapshot.links.push_back(RenderGraphLink{
            link.fromNodeId,
            link.fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }
    return snapshot;
}

bool EditorModule::TryCopyImageNodePixels(int sourceNodeId, std::vector<unsigned char>& outPixels, int& outW, int& outH, int& outChannels) const {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *sourceNode);
        const Raw::RawMetadata emptyMetadata;
        const Raw::RawMetadata& metadata = rawSourceNode ? rawSourceNode->rawSource.metadata : emptyMetadata;
        
        const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
        const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
        
        const int effectiveOrientation = ResolveRawDevelopOrientation(metadata, sourceNode->rawDevelop.settings);
        const bool swaps = (effectiveOrientation == 5 || effectiveOrientation == 6 || effectiveOrientation == 7 || effectiveOrientation == 8);
        if (sourceNode->rawDevelop.settings.rotateToFitFrame) {
            outW = visibleWidth;
            outH = visibleHeight;
        } else {
            outW = swaps ? visibleHeight : visibleWidth;
            outH = swaps ? visibleWidth : visibleHeight;
        }
        outChannels = 4;
        outPixels = BuildTransparentPixels(outW, outH);
        return !outPixels.empty();
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        outW = Raw::DisplayWidth(sourceNode->rawSource.metadata);
        outH = Raw::DisplayHeight(sourceNode->rawSource.metadata);
        outChannels = 4;
        outPixels = BuildTransparentPixels(outW, outH);
        return !outPixels.empty();
    }

    if (sourceNode->kind != EditorNodeGraph::NodeKind::Image ||
        sourceNode->image.pixels.empty() ||
        sourceNode->image.width <= 0 ||
        sourceNode->image.height <= 0) {
        return false;
    }

    outPixels = sourceNode->image.pixels;
    outW = sourceNode->image.width;
    outH = sourceNode->image.height;
    outChannels = std::max(1, sourceNode->image.channels);
    return true;
}

bool EditorModule::TryResolveReferenceSourcePixels(
    int nodeId,
    const std::string& socketId,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeId(nodeId, socketId);
    return TryCopyImageNodePixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryResolveReferenceSourcePixelsForOutput(
    int outputNodeId,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeIdForOutput(outputNodeId);
    return TryCopyImageNodePixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryResolveReferenceSourceDimensions(
    int nodeId,
    const std::string& socketId,
    int& outW,
    int& outH) const {
    outW = 0;
    outH = 0;

    const int referenceSourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeId(nodeId, socketId);
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(referenceSourceNodeId);
    if (!sourceNode) {
        return false;
    }

    switch (sourceNode->kind) {
        case EditorNodeGraph::NodeKind::Image:
            outW = sourceNode->image.width;
            outH = sourceNode->image.height;
            return outW > 0 && outH > 0;
        case EditorNodeGraph::NodeKind::RawSource:
            outW = Raw::DisplayWidth(sourceNode->rawSource.metadata);
            outH = Raw::DisplayHeight(sourceNode->rawSource.metadata);
            return outW > 0 && outH > 0;
        case EditorNodeGraph::NodeKind::RawDevelop: {
            const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *sourceNode);
            const Raw::RawMetadata emptyMetadata;
            const Raw::RawMetadata& metadata = rawSourceNode ? rawSourceNode->rawSource.metadata : emptyMetadata;
            const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
            const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
            const int effectiveOrientation = ResolveRawDevelopOrientation(metadata, sourceNode->rawDevelop.settings);
            const bool swaps = (effectiveOrientation == 5 || effectiveOrientation == 6 || effectiveOrientation == 7 || effectiveOrientation == 8);
            if (sourceNode->rawDevelop.settings.rotateToFitFrame) {
                outW = visibleWidth;
                outH = visibleHeight;
            } else {
                outW = swaps ? visibleHeight : visibleWidth;
                outH = swaps ? visibleWidth : visibleHeight;
            }
            return outW > 0 && outH > 0;
        }
        default: {
            std::vector<unsigned char> pixels;
            int channels = 4;
            return TryCopyImageNodePixels(referenceSourceNodeId, pixels, outW, outH, channels);
        }
    }
}

std::vector<int> EditorModule::CollectHdrMergeNodesForOutput(int outputNodeId) const {
    std::vector<int> hdrNodes;
    if (outputNodeId <= 0) {
        return hdrNodes;
    }

    std::unordered_set<int> seenNodes;
    std::unordered_set<int> seenHdrNodes;
    std::function<void(int)> visit = [&](int nodeId) {
        if (!seenNodes.insert(nodeId).second) {
            return;
        }

        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            return;
        }
        if (node->kind == EditorNodeGraph::NodeKind::HdrMerge && seenHdrNodes.insert(nodeId).second) {
            hdrNodes.push_back(nodeId);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : m_NodeGraph.GetSockets(*node, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
                continue;
            }
            if (const EditorNodeGraph::Link* input = m_NodeGraph.FindAnyInputLink(nodeId, socket.id)) {
                visit(input->fromNodeId);
            }
        }
    };

    visit(outputNodeId);
    return hdrNodes;
}

EditorModule::HdrMergeConnectionTopology EditorModule::ResolveHdrMergeConnectionTopology(const EditorNodeGraph::Node& node) const {
    HdrMergeConnectionTopology topology;
    if (node.kind != EditorNodeGraph::NodeKind::HdrMerge) {
        return topology;
    }

    topology.hasInput1 = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kHdrMergeInput1SocketId) != nullptr;
    topology.hasInput2 = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kHdrMergeInput2SocketId) != nullptr;
    topology.hasInput3 = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kHdrMergeInput3SocketId) != nullptr;
    topology.hasGap = topology.hasInput3 && !topology.hasInput2;
    topology.usesInput3 = topology.hasInput1 && topology.hasInput2 && topology.hasInput3 && !topology.hasGap;
    if (topology.hasInput1 && topology.hasInput2) {
        topology.activeInputCount = topology.usesInput3 ? 3 : 2;
    }
    return topology;
}

EditorModule::HdrMergeNodeStatus EditorModule::BuildHdrMergeNodeStatus(const EditorNodeGraph::Node& node) const {
    HdrMergeNodeStatus status;
    status.debugView = node.hdrMerge.settings.debugView;
    const HdrMergeConnectionTopology topology = ResolveHdrMergeConnectionTopology(node);
    status.metadataNormalizationReady = node.hdrMerge.settings.exposureMode != Raw::HdrMergeExposureMode::Metadata;
    status.automaticReliabilityReady = !node.hdrMerge.settings.autoReliability;

    struct HdrMergeInputContext {
        bool hasRawMetadata = false;
        bool hasCaptureExposure = false;
        float developExposureStops = 0.0f;
        Raw::RawMetadata metadata;
    };
    std::array<HdrMergeInputContext, 3> contexts {};

    const auto resolveInputContext = [&](int sourceNodeId) {
        HdrMergeInputContext context;
        const int referenceNodeId = m_NodeGraph.ResolveReferenceSourceNodeId(sourceNodeId, EditorNodeGraph::kImageOutputSocketId);
        const EditorNodeGraph::Node* referenceNode = m_NodeGraph.FindNode(referenceNodeId);
        if (!referenceNode) {
            return context;
        }

        if (referenceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
            context.hasRawMetadata = true;
            context.metadata = referenceNode->rawSource.metadata;
        } else if (referenceNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            context.developExposureStops = referenceNode->rawDevelop.settings.exposureStops;
            const EditorNodeGraph::Link* rawInput = m_NodeGraph.FindInputLink(referenceNode->id, EditorNodeGraph::kRawInputSocketId);
            std::unordered_set<int> visitedRawNodes;
            while (rawInput && visitedRawNodes.insert(rawInput->fromNodeId).second) {
                const EditorNodeGraph::Node* rawNode = m_NodeGraph.FindNode(rawInput->fromNodeId);
                if (!rawNode) {
                    break;
                }
                if (rawNode->kind == EditorNodeGraph::NodeKind::RawSource) {
                    context.hasRawMetadata = true;
                    context.metadata = rawNode->rawSource.metadata;
                    break;
                }
                if (rawNode->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
                    break;
                }
                rawInput = m_NodeGraph.FindInputLink(rawNode->id, EditorNodeGraph::kRawInputSocketId);
            }
        }

        if (context.hasRawMetadata) {
            context.hasCaptureExposure = HasHdrMergeCaptureExposureMetadata(context.metadata);
        }
        return context;
    };

    const auto fillInput = [&](std::size_t index, const char* socketId, const char* label, bool active) {
        HdrMergeInputSummary& input = status.inputs[index];
        input.socketId = socketId;
        input.label = label;
        input.active = active;
        const EditorNodeGraph::Link* link = m_NodeGraph.FindInputLink(node.id, socketId);
        input.connected = link != nullptr;
        input.sourceNodeId = link ? link->fromNodeId : -1;
        input.sourceLabel = "Missing";
        input.compatible = true;
        if (const EditorNodeGraph::Node* sourceNode = link ? m_NodeGraph.FindNode(link->fromNodeId) : nullptr) {
            input.sourceLabel = sourceNode->title.empty() ? std::string("Node ") + std::to_string(sourceNode->id) : sourceNode->title;
        } else if (!active) {
            input.sourceLabel = "Inactive";
        }
        if (input.connected) {
            TryResolveReferenceSourceDimensions(node.id, socketId, input.width, input.height);
            contexts[index] = resolveInputContext(input.sourceNodeId);
            input.hasRawMetadata = contexts[index].hasRawMetadata;
            input.hasCaptureExposure = contexts[index].hasCaptureExposure;
            if (input.hasRawMetadata) {
                const std::string summary =
                    BuildHdrMergeCaptureMetadataSummary(contexts[index].metadata, contexts[index].developExposureStops);
                input.metadataSummary = summary.empty()
                    ? std::string("RAW metadata is available, but capture exposure fields are incomplete.")
                    : summary;
            } else {
                input.metadataSummary = "Scene-linear input only; automatic RAW-derived exposure and reliability may be unavailable.";
            }
        }
    };

    fillInput(0, EditorNodeGraph::kHdrMergeInput1SocketId, "Image 1", true);
    fillInput(1, EditorNodeGraph::kHdrMergeInput2SocketId, "Image 2", true);
    fillInput(2, EditorNodeGraph::kHdrMergeInput3SocketId, "Image 3", topology.hasInput3);

    std::vector<std::string> metadataMissingInputs;
    std::vector<std::string> reliabilityMissingInputs;
    std::vector<float> apertureValues;
    std::array<bool, 3> activeConnectedInputs { false, false, false };
    for (std::size_t index = 0; index < status.inputs.size(); ++index) {
        const HdrMergeInputSummary& input = status.inputs[index];
        const HdrMergeInputContext& context = contexts[index];
        if (!input.active || !input.connected) {
            continue;
        }
        activeConnectedInputs[index] = true;
        if (node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata && !input.hasCaptureExposure) {
            metadataMissingInputs.push_back(input.label);
        }
        if (node.hdrMerge.settings.autoReliability && !input.hasRawMetadata) {
            reliabilityMissingInputs.push_back(input.label);
        }
        if (context.hasRawMetadata && context.metadata.hasApertureFNumber) {
            apertureValues.push_back(context.metadata.apertureFNumber);
        }
    }

    if (node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata) {
        if (metadataMissingInputs.empty()) {
            status.metadataNormalizationReady = true;
            status.normalizationMessage = "Automatic normalization is ready for all active frames.";
        } else {
            status.metadataNormalizationReady = false;
            status.normalizationMessage =
                "Automatic normalization falls back to manual EV for " + JoinSummaryList(metadataMissingInputs) + ".";
        }
    } else {
        status.normalizationMessage = "Manual EV normalization is active.";
    }

    if (node.hdrMerge.settings.autoReliability) {
        if (reliabilityMissingInputs.empty()) {
            status.automaticReliabilityReady = true;
            status.reliabilityMessage = "Automatic reliability is using RAW metadata for all active frames.";
        } else {
            status.automaticReliabilityReady = false;
            status.reliabilityMessage =
                "Automatic reliability falls back to manual thresholds for " + JoinSummaryList(reliabilityMissingInputs) + ".";
        }
    } else {
        status.reliabilityMessage = "Manual reliability thresholds are active.";
    }

    const bool usingMetadataNormalization =
        node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata && metadataMissingInputs.empty();
    std::array<float, 3> resolvedNormalizationEv {
        node.hdrMerge.settings.manualExposureEv[0],
        node.hdrMerge.settings.manualExposureEv[1],
        node.hdrMerge.settings.manualExposureEv[2]
    };
    if (usingMetadataNormalization) {
        std::array<float, 3> absoluteExposureEv { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 3; ++i) {
            if (!activeConnectedInputs[i]) {
                continue;
            }
            absoluteExposureEv[i] = ComputeHdrMergeCaptureExposureEvForStatus(contexts[i].metadata) +
                contexts[i].developExposureStops +
                node.hdrMerge.settings.exposureOffsetEv[i];
        }
        const float exposureAnchor = SelectHdrMergeExposureAnchorForStatus(absoluteExposureEv, activeConnectedInputs);
        for (int i = 0; i < 3; ++i) {
            if (!activeConnectedInputs[i]) {
                continue;
            }
            resolvedNormalizationEv[i] = absoluteExposureEv[i] - exposureAnchor;
        }
    }

    for (int i = 0; i < 3; ++i) {
        HdrMergeInputSummary& input = status.inputs[i];
        if (!activeConnectedInputs[i]) {
            continue;
        }

        char buffer[96];
        if (usingMetadataNormalization) {
            std::snprintf(buffer, sizeof(buffer), "Normalized %+.2f EV relative to the merge reference.", resolvedNormalizationEv[i]);
        } else if (node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata) {
            std::snprintf(buffer, sizeof(buffer), "Manual fallback %+.2f EV.", resolvedNormalizationEv[i]);
        } else {
            std::snprintf(buffer, sizeof(buffer), "Manual normalization %+.2f EV.", resolvedNormalizationEv[i]);
        }
        input.normalizationSummary = buffer;
    }

    if (apertureValues.size() >= 2) {
        const float referenceAperture = apertureValues.front();
        for (float aperture : apertureValues) {
            if (std::fabs(aperture - referenceAperture) > 0.05f) {
                status.warningMessage =
                    "Aperture varies across the active frames. Automatic exposure matching may need per-frame offsets.";
                break;
            }
        }
    }

    const int previewOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    if (previewOutputNodeId > 0) {
        const std::vector<int> activeHdrNodes = CollectHdrMergeNodesForOutput(previewOutputNodeId);
        status.feedsActiveOutput = std::find(activeHdrNodes.begin(), activeHdrNodes.end(), node.id) != activeHdrNodes.end();
    }

    if (!status.inputs[0].connected) {
        status.state = HdrMergeRenderState::BlockedMissingInput;
        status.message = "Needs Image 1";
        return status;
    }
    if (!status.inputs[1].connected) {
        status.state = HdrMergeRenderState::BlockedMissingInput;
        status.message = "Needs Image 2";
        return status;
    }
    if (topology.hasGap) {
        status.state = HdrMergeRenderState::BlockedMissingInput;
        status.message = "Image 3 requires Image 2";
        return status;
    }

    int referenceWidth = status.inputs[0].width;
    int referenceHeight = status.inputs[0].height;
    for (HdrMergeInputSummary& input : status.inputs) {
        if ((!input.active && input.socketId == EditorNodeGraph::kHdrMergeInput3SocketId) || !input.connected) {
            continue;
        }
        if (referenceWidth <= 0 || referenceHeight <= 0 || input.width <= 0 || input.height <= 0) {
            status.state = HdrMergeRenderState::Failed;
            status.message = "Reference canvas unavailable";
            return status;
        }
        input.compatible = (input.width == referenceWidth && input.height == referenceHeight);
        if (!input.compatible) {
            status.state = HdrMergeRenderState::IncompatibleInput;
            status.message = "Dimension mismatch";
            return status;
        }
    }

    const std::uint64_t dirtyGeneration = GetNodeDirtyGeneration(node.id);
    const std::uint64_t requestedGeneration =
        m_HdrMergeRequestedGenerations.count(node.id) ? m_HdrMergeRequestedGenerations.at(node.id) : 0;
    const std::uint64_t completedGeneration =
        m_HdrMergeCompletedGenerations.count(node.id) ? m_HdrMergeCompletedGenerations.at(node.id) : 0;
    status.hasRenderedResult = completedGeneration > 0;
    status.stale = completedGeneration > 0 && completedGeneration < dirtyGeneration;

    const auto failureIt = m_HdrMergeFailureMessages.find(node.id);
    const bool hasFailureMessage = failureIt != m_HdrMergeFailureMessages.end() &&
        !failureIt->second.empty() &&
        requestedGeneration > completedGeneration;

    if (!status.feedsActiveOutput) {
        if (hasFailureMessage) {
            status.state = HdrMergeRenderState::Failed;
            status.message = failureIt->second;
            return status;
        }
        status.state = HdrMergeRenderState::Ready;
        status.message = status.stale ? "Stale" : "Ready";
        return status;
    }
    if (completedGeneration >= dirtyGeneration && dirtyGeneration > 0) {
        status.state = HdrMergeRenderState::Rendered;
        status.message = "Rendered";
        return status;
    }
    if (m_HdrMergeRenderingNodeIds.count(node.id) > 0) {
        status.state = HdrMergeRenderState::Rendering;
        status.message = "Rendering";
        return status;
    }
    if (requestedGeneration >= dirtyGeneration && dirtyGeneration > 0) {
        status.state = HdrMergeRenderState::Queued;
        status.message = "Queued";
        return status;
    }
    if (hasFailureMessage) {
        status.state = HdrMergeRenderState::Failed;
        status.message = failureIt->second;
        status.stale = true;
        return status;
    }

    status.state = HdrMergeRenderState::Ready;
    status.message = status.stale ? "Stale" : "Ready";
    return status;
}

EditorModule::HdrMergeNodeStatus EditorModule::GetHdrMergeNodeStatus(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::HdrMerge) {
        return {};
    }
    return BuildHdrMergeNodeStatus(*node);
}


void EditorModule::EnterSingleOutputPreviewMode() {
    ClearCompositeTransientInteractionState();
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_CompositeExportBoundsEditMode = false;
    m_Viewport.ResetSinglePreviewState();
    m_HoverFade = 0.0f;

    const int previewOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    if (previewOutputNodeId <= 0) {
        m_Pipeline.ClearOutput();
        MarkRenderDirty();
        return;
    }

    RefreshCompletedChainCacheIfNeeded();
    const auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [previewOutputNodeId](const CachedCompositeChainState& chain) {
            return chain.info.outputNodeId == previewOutputNodeId;
        });
    if (chainIt == m_CachedCompletedChains.end()) {
        MarkRenderDirty();
        return;
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chainIt->info.sourceNodeId);
    if (sourceNode &&
        sourceNode->kind == EditorNodeGraph::NodeKind::Image &&
        !sourceNode->image.pixels.empty() &&
        sourceNode->image.width > 0 &&
        sourceNode->image.height > 0) {
        LoadSourceFromPixels(
            sourceNode->image.pixels.data(),
            sourceNode->image.width,
            sourceNode->image.height,
            sourceNode->image.channels);
        return;
    }

    int outW = 0;
    int outH = 0;
    (void)GetCompositePixelsForOutputNode(previewOutputNodeId, outW, outH);
    if (outW > 0 && outH > 0) {
        const std::vector<unsigned char> transparentPixels = BuildTransparentPixels(outW, outH);
        LoadSourceFromPixels(transparentPixels.data(), outW, outH, 4);
    } else {
        MarkRenderDirty();
    }
}

void EditorModule::HandleViewportModeTransition(ViewportMode previousMode, ViewportMode currentMode) {
    if (previousMode == currentMode) {
        return;
    }
    if (previousMode == ViewportMode::CompositeCanvas && currentMode == ViewportMode::SingleOutputPreview) {
        EnterSingleOutputPreviewMode();
    } else if (previousMode == ViewportMode::SingleOutputPreview && currentMode == ViewportMode::CompositeCanvas) {
        m_HoverFade = 0.0f;
        ClearCompositeTransientInteractionState();
    }
    m_LastViewportMode = currentMode;
}

std::size_t EditorModule::BuildCompositeChainFingerprint(const EditorNodeGraph::CompletedChainInfo& chain) const {
    std::size_t fingerprint = HashValue(chain.outputNodeId);
    HashCombine(fingerprint, HashValue(chain.terminalNodeId));
    HashCombine(fingerprint, HashValue(chain.sourceNodeId));
    for (int nodeId : chain.nodeIds) {
        HashCombine(fingerprint, HashValue(nodeId));
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            continue;
        }
        HashCombine(fingerprint, HashValue(static_cast<int>(node->kind)));
        switch (node->kind) {
            case EditorNodeGraph::NodeKind::Image:
                HashCombine(fingerprint, HashValue(node->image.width));
                HashCombine(fingerprint, HashValue(node->image.height));
                HashCombine(fingerprint, HashValue(node->image.channels));
                HashCombine(fingerprint, HashValue(node->title));
                break;
            case EditorNodeGraph::NodeKind::Layer:
                if (node->layerIndex >= 0 && node->layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node->layerIndex]) {
                    HashCombine(fingerprint, HashValue(m_Layers[node->layerIndex]->Serialize().dump()));
                }
                break;
            case EditorNodeGraph::NodeKind::Mix:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->mixBlendMode)));
                HashCombine(fingerprint, HashValue(node->mixFactor));
                break;
            case EditorNodeGraph::NodeKind::DataMath:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->dataMathMode)));
                HashCombine(fingerprint, HashValue(node->dataMathSettings.constantA));
                HashCombine(fingerprint, HashValue(node->dataMathSettings.constantB));
                HashCombine(fingerprint, HashValue(node->dataMathSettings.minValue));
                HashCombine(fingerprint, HashValue(node->dataMathSettings.maxValue));
                HashCombine(fingerprint, HashValue(node->dataMathSettings.outMin));
                HashCombine(fingerprint, HashValue(node->dataMathSettings.outMax));
                break;
            case EditorNodeGraph::NodeKind::HdrMerge:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->hdrMerge.settings.debugView)));
                HashCombine(fingerprint, HashValue(static_cast<int>(node->hdrMerge.settings.alignmentMode)));
                HashCombine(fingerprint, HashValue(static_cast<int>(node->hdrMerge.settings.exposureMode)));
                HashCombine(fingerprint, HashValue(static_cast<int>(node->hdrMerge.settings.referenceMode)));
                HashCombine(fingerprint, HashValue(static_cast<int>(node->hdrMerge.settings.deghostMode)));
                HashCombine(fingerprint, HashValue(static_cast<int>(node->hdrMerge.settings.motionPriority)));
                for (float ev : node->hdrMerge.settings.manualExposureEv) {
                    HashCombine(fingerprint, HashValue(ev));
                }
                for (float ev : node->hdrMerge.settings.exposureOffsetEv) {
                    HashCombine(fingerprint, HashValue(ev));
                }
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.autoReliability));
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.clipThreshold));
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.clipFeather));
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.blackThreshold));
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.blackFeather));
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.readNoise));
                HashCombine(fingerprint, HashValue(node->hdrMerge.settings.noiseAware));
                break;
            case EditorNodeGraph::NodeKind::ImageGenerator:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->imageGeneratorKind)));
                HashCombine(fingerprint, HashValue(node->imageGeneratorSettings.angle));
                HashCombine(fingerprint, HashValue(node->imageGeneratorSettings.offset));
                for (float channel : node->imageGeneratorSettings.colorA) {
                    HashCombine(fingerprint, HashValue(channel));
                }
                for (float channel : node->imageGeneratorSettings.colorB) {
                    HashCombine(fingerprint, HashValue(channel));
                }
                break;
            default:
                break;
        }
    }
    return fingerprint;
}

std::string EditorModule::BuildCompositeChainLabel(const EditorNodeGraph::CompletedChainInfo& chain) const {
    if (chain.outputNodeId <= 0) {
        return "Output";
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chain.sourceNodeId);
    const EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(chain.outputNodeId);
    const std::string sourceLabel = sourceNode && !sourceNode->title.empty()
        ? sourceNode->title
        : ("Source " + std::to_string(chain.sourceNodeId));
    const std::string outputLabel = outputNode && !outputNode->title.empty()
        ? outputNode->title
        : ("Output " + std::to_string(chain.outputNodeId));
    return sourceLabel + " -> " + outputLabel;
}

std::string EditorModule::BuildCompositeChainLabel(int outputNodeId) const {
    RefreshCompletedChainCacheIfNeeded();
    auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [outputNodeId](const CachedCompositeChainState& chain) { return chain.info.outputNodeId == outputNodeId; });
    if (chainIt == m_CachedCompletedChains.end()) {
        return "Output " + std::to_string(outputNodeId);
    }
    return chainIt->label.empty() ? BuildCompositeChainLabel(chainIt->info) : chainIt->label;
}

bool EditorModule::HasCompositeNode() const {
    return false;
}

void EditorModule::EnsureCompositeNode() {
    // Deprecated: settings have been fully migrated to the unified editor settings panel.
}

void EditorModule::BeginLibraryLoadReveal() {
    m_LibraryLoadRevealStartTime = -1.0;
    m_LibraryLoadRevealPendingFirstFrame = true;
    m_LibraryLoadRevealLayoutPending = true;
    m_LibraryLoadCanvasRevealAlpha = 0.0f;
    m_LibraryLoadGraphRevealAlpha = 0.0f;
    m_LibraryLoadToolbarRevealAlpha = 0.0f;
    m_NodeGraphFullscreen = false;
    m_TargetSubWindow = EditorSubWindow::NodeGraph;
    m_ActiveSubWindow = EditorSubWindow::NodeGraph;
    m_TargetComplexNodeId = -1;
    m_ActiveComplexNodeId = -1;
    m_LastSplitTargetSubWindow = EditorSubWindow::NodeGraph;
    m_LastSplitTargetComplexNodeId = -1;
    m_LeftPaneWidth = 520.0f;
    m_LastUserNodeGraphWidth = 520.0f;
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimating = false;
    m_DraggingSplitHandle = false;
    m_SplitHandlePressed = false;
    m_SplitHandleMoved = false;
    m_SubWindowTransitionAlpha = 1.0f;
    m_SubWindowTransitionFadingOut = false;
}


void EditorModule::RenderUI() {
    ConsumeRenderWorkerResults();

    // User Activity Tracking & Auto-Save Check
    {
        const ImGuiIO& io = ImGui::GetIO();
        bool userActive = false;
        // Check for any keypress
        for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key = (ImGuiKey)(key + 1)) {
            if (ImGui::IsKeyPressed(key)) {
                userActive = true;
                break;
            }
        }
        // Check for mouse down
        if (!userActive) {
            for (int i = 0; i < 5; i++) {
                if (ImGui::IsMouseDown(i)) {
                    userActive = true;
                    break;
                }
            }
        }
        // Check for mouse movement or characters typed
        if (!userActive && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.InputQueueCharacters.Size > 0)) {
            userActive = true;
        }

        if (userActive) {
            m_LastUserActionTime = ImGui::GetTime();
        }

        if (m_Dirty && !m_CurrentProjectFileName.empty() && !Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
            double idleTime = ImGui::GetTime() - m_LastUserActionTime;
            if (idleTime >= 10.0 && idleTime < 30.0 && m_LastAutoSaveTime < m_LastUserActionTime) {
                LibraryManager::Get().RequestSaveProject(m_CurrentProjectName, this, m_CurrentProjectFileName);
                m_LastAutoSaveTime = ImGui::GetTime();
            }
        }
    }

    // Safety fallback: if ExportSettings is active or targeted but we have less than 2 completed chains, fallback to NodeGraph
    if (GetCompletedChainCount() < 2) {
        if (m_ActiveSubWindow == EditorSubWindow::ExportSettings) {
            m_ActiveSubWindow = EditorSubWindow::NodeGraph;
        }
        if (m_TargetSubWindow == EditorSubWindow::ExportSettings) {
            m_TargetSubWindow = EditorSubWindow::NodeGraph;
        }
    }

    // Safety fallback: if ComplexNode is active or targeted but the node no longer exists in the graph, fallback to NodeGraph
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode) {
        if (!m_NodeGraph.FindNode(m_ActiveComplexNodeId)) {
            m_ActiveSubWindow = EditorSubWindow::NodeGraph;
        }
    }
    if (m_TargetSubWindow == EditorSubWindow::ComplexNode) {
        if (!m_NodeGraph.FindNode(m_TargetComplexNodeId)) {
            m_TargetSubWindow = EditorSubWindow::NodeGraph;
        }
    }

    // Update Sub-Window Transition
    if (m_ActiveSubWindow != m_TargetSubWindow || m_SubWindowTransitionFadingOut || m_SubWindowTransitionAlpha < 1.0f) {
        float dt = ImGui::GetIO().DeltaTime;
        if (m_SubWindowTransitionFadingOut) {
            m_SubWindowTransitionAlpha -= dt * 6.0f;
            if (m_SubWindowTransitionAlpha <= 0.0f) {
                m_SubWindowTransitionAlpha = 0.0f;
                m_ActiveSubWindow = m_TargetSubWindow;
                m_ActiveComplexNodeId = m_TargetComplexNodeId;
                m_SubWindowTransitionFadingOut = false; // Start fading in
            }
        } else {
            m_SubWindowTransitionAlpha += dt * 6.0f;
            if (m_SubWindowTransitionAlpha >= 1.0f) {
                m_SubWindowTransitionAlpha = 1.0f;
            }
        }
    }

    if (m_LibraryLoadRevealPendingFirstFrame) {
        m_LibraryLoadRevealPendingFirstFrame = false;
        m_LibraryLoadRevealStartTime = ImGui::GetTime();
    }

    if (m_LibraryLoadRevealStartTime >= 0.0) {
        const double elapsed = ImGui::GetTime() - m_LibraryLoadRevealStartTime;
        auto easeOutCubic = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return 1.0f - std::pow(1.0f - value, 3.0f);
        };
        m_LibraryLoadCanvasRevealAlpha = easeOutCubic(static_cast<float>(elapsed / 0.78));
        m_LibraryLoadGraphRevealAlpha = easeOutCubic(static_cast<float>((elapsed - 0.58) / 0.62));
        m_LibraryLoadToolbarRevealAlpha = easeOutCubic(static_cast<float>((elapsed - 1.22) / 0.36));
        if (elapsed >= 1.90) {
            m_LibraryLoadRevealStartTime = -1.0;
            m_LibraryLoadRevealPendingFirstFrame = false;
            m_LibraryLoadRevealLayoutPending = false;
            m_LibraryLoadCanvasRevealAlpha = 1.0f;
            m_LibraryLoadGraphRevealAlpha = 1.0f;
            m_LibraryLoadToolbarRevealAlpha = 1.0f;
        }
    } else {
        m_LibraryLoadCanvasRevealAlpha = 1.0f;
        m_LibraryLoadGraphRevealAlpha = 1.0f;
        m_LibraryLoadToolbarRevealAlpha = 1.0f;
    }

    const bool hotkeyOnePressed = CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_1, false);
    const bool hotkeyTwoPressed = CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_2, false);
    const bool hotkeyThreePressed = CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_3, false);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetWorkspaceBaseColor());
    ImGui::BeginChild("StackEditorWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const ImVec2 workspacePos = ImGui::GetCursorScreenPos();
    const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
    const ImVec4 workspaceColor = GetWorkspaceBaseColor();
    const ImU32 workspaceColorU32 = ImGui::ColorConvertFloat4ToU32(workspaceColor);
    const ViewportMode viewportMode = GetViewportMode();
    HandleViewportModeTransition(m_LastViewportMode, viewportMode);
    const bool compositeViewportMode = viewportMode == ViewportMode::CompositeCanvas;
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    ImGui::GetWindowDrawList()->AddRectFilled(
        workspacePos,
        ImVec2(workspacePos.x + workspaceSize.x, workspacePos.y + workspaceSize.y),
        workspaceColorU32);
    const float splitGap = 32.0f;
    const float minLeftWidth = 260.0f;
    const float minRightWidth = 420.0f;
    const float maxLeftWidth = std::max(minLeftWidth, workspaceSize.x - minRightWidth - splitGap);

    if (m_LibraryLoadRevealLayoutPending) {
        int imgW = m_Pipeline.GetCanvasWidth();
        int imgH = m_Pipeline.GetCanvasHeight();
        float targetLeftPaneWidth = std::clamp(520.0f, minLeftWidth, maxLeftWidth);
        if (imgW > 0 && imgH > 0) {
            const float paddingY = 32.0f;
            const float paddingX = 36.0f;
            const float availY = std::max(100.0f, workspaceSize.y - paddingY);
            const float imageAspect = static_cast<float>(imgW) / std::max(1.0f, static_cast<float>(imgH));
            const float displayWidth = availY * imageAspect;
            const float optimalRightWidth = displayWidth + paddingX;
            const float maxRightWidth = std::max(minRightWidth, workspaceSize.x - minLeftWidth - splitGap);
            const float constrainedRightWidth = std::clamp(optimalRightWidth, minRightWidth, maxRightWidth);
            targetLeftPaneWidth = workspaceSize.x - splitGap - constrainedRightWidth;
        }

        targetLeftPaneWidth = std::clamp(targetLeftPaneWidth, minLeftWidth, maxLeftWidth);
        m_LeftPaneWidth = targetLeftPaneWidth;
        m_LastUserNodeGraphWidth = targetLeftPaneWidth;
        m_SplitAutoAnimFrom = targetLeftPaneWidth;
        m_SplitAutoAnimTo = targetLeftPaneWidth;
        m_SplitAutoAnimating = false;
        m_LibraryLoadRevealLayoutPending = false;
    }

    auto startSplitAutoAnimation = [&](const float targetWidth, const CompositeEdgeSnapMode snapMode = CompositeEdgeSnapMode::None) {
        m_SplitAutoAnimFrom = m_LeftPaneWidth;
        m_SplitAutoAnimTo = std::clamp(targetWidth, 0.0f, workspaceSize.x);
        m_SplitAutoAnimSnapMode = snapMode;
        m_SplitAutoAnimStartTime = ImGui::GetTime();
        m_SplitAutoAnimating = true;
    };

    if (hotkeyOnePressed) {
        const bool graphSelected = m_TargetSubWindow == EditorSubWindow::NodeGraph &&
            (m_ActiveSubWindow == EditorSubWindow::NodeGraph || !m_SubWindowTransitionFadingOut);
        if (!graphSelected || m_ActiveSubWindow != EditorSubWindow::NodeGraph) {
            m_NodeGraphFullscreen = false;
            SwitchToSubWindow(EditorSubWindow::NodeGraph);
        } else if (m_NodeGraphFullscreen || m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
            m_NodeGraphFullscreen = false;
            const float restoreWidth = (m_LastUserNodeGraphWidth > 0.0f)
                ? std::clamp(m_LastUserNodeGraphWidth, minLeftWidth, maxLeftWidth)
                : std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
            startSplitAutoAnimation(restoreWidth);
        } else {
            if (m_LeftPaneWidth > 0.0f && m_LeftPaneWidth < workspaceSize.x - 2.0f) {
                m_LastUserNodeGraphWidth = std::clamp(m_LeftPaneWidth, minLeftWidth, maxLeftWidth);
            }
            m_NodeGraphFullscreen = true;
            startSplitAutoAnimation(workspaceSize.x);
        }
    }
    if (hotkeyTwoPressed && GetCompletedChainCount() >= 2) {
        m_NodeGraphFullscreen = false;
        m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
        m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
        SwitchToSubWindow(EditorSubWindow::NodeGraph);
        const float restoreWidth = (m_LastUserNodeGraphWidth > 0.0f)
            ? std::clamp(m_LastUserNodeGraphWidth, minLeftWidth, maxLeftWidth)
            : std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
        startSplitAutoAnimation(restoreWidth);
    }
    if (hotkeyThreePressed) {
        m_NodeGraphFullscreen = false;
        SwitchToSubWindow(EditorSubWindow::Settings);
    }

    // Transition detection for split auto-animation to optimal sizes
    if (m_TargetSubWindow != m_LastSplitTargetSubWindow || m_TargetComplexNodeId != m_LastSplitTargetComplexNodeId) {
        m_LastSplitTargetSubWindow = m_TargetSubWindow;
        m_LastSplitTargetComplexNodeId = m_TargetComplexNodeId;

        float targetWidth = m_LeftPaneWidth;
        if (m_TargetSubWindow == EditorSubWindow::NodeGraph) {
            if (m_NodeGraphFullscreen) {
                targetWidth = workspaceSize.x;
            } else {
                if (m_LastUserNodeGraphWidth <= 0.0f) {
                    m_LastUserNodeGraphWidth = std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
                }
                targetWidth = m_LastUserNodeGraphWidth;
            }
        } else if (m_TargetSubWindow == EditorSubWindow::ExportSettings) {
            targetWidth = 360.0f;
        } else if (m_TargetSubWindow == EditorSubWindow::Settings) {
            targetWidth = 360.0f;
        } else if (m_TargetSubWindow == EditorSubWindow::ComplexNode) {
            const EditorNodeGraph::Node* targetComplexNode =
                m_TargetComplexNodeId > 0 ? m_NodeGraph.FindNode(m_TargetComplexNodeId) : nullptr;
            targetWidth = targetComplexNode && targetComplexNode->kind == EditorNodeGraph::NodeKind::CustomMask
                ? (workspaceSize.x - splitGap) * 0.5f
                : 470.0f;
        }

        if (!(m_TargetSubWindow == EditorSubWindow::NodeGraph && m_NodeGraphFullscreen)) {
            targetWidth = std::clamp(targetWidth, minLeftWidth, maxLeftWidth);
        }

        m_SplitAutoAnimFrom = m_LeftPaneWidth;
        m_SplitAutoAnimTo = targetWidth;
        m_SplitAutoAnimStartTime = ImGui::GetTime();
        m_SplitAutoAnimating = true;
        if (compositeViewportMode) {
            m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
        }
    }

    if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly && !m_DraggingSplitHandle && !m_SplitAutoAnimating) {
        m_LeftPaneWidth = workspaceSize.x;
    } else if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly && !m_DraggingSplitHandle && !m_SplitAutoAnimating) {
        m_LeftPaneWidth = 0.0f;
    } else {
        if (m_DraggingSplitHandle || m_SplitAutoAnimating) {
            m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, 0.0f, workspaceSize.x);
        } else {
            if (m_NodeGraphFullscreen && !compositeViewportMode) {
                m_LeftPaneWidth = workspaceSize.x;
            } else if (m_LeftPaneWidth <= 0.0f && !compositeViewportMode) {
                m_LeftPaneWidth = std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
            } else {
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, minLeftWidth, maxLeftWidth);
            }
        }
    }

    if (CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        m_SpacebarPressTime = ImGui::GetTime();
        m_SpacebarHeld = true;
    }

    if (m_SpacebarHeld) {
        if (!ImGui::IsKeyDown(ImGuiKey_Space)) {
            const float holdTime = static_cast<float>(ImGui::GetTime() - m_SpacebarPressTime);
            const float paneHeight = std::max(0.0f, workspaceSize.y);
            if (holdTime >= 0.4f) {
                HandleSpacebarLongPress(workspaceSize.x, paneHeight, minLeftWidth, maxLeftWidth, splitGap);
            } else {
                HandleSpacebarPress(workspaceSize.x, paneHeight, minLeftWidth, maxLeftWidth, splitGap);
            }
            m_SpacebarHeld = false;
        }
    }

    const float effectiveSplitGap = (compositeViewportMode || m_CompositeEdgeSnapMode != CompositeEdgeSnapMode::None || m_DraggingSplitHandle || m_SplitAutoAnimating)
        ? splitGap * std::clamp(
            std::min(m_LeftPaneWidth, std::max(0.0f, workspaceSize.x - m_LeftPaneWidth)) / std::max(1.0f, splitGap),
            0.0f,
            1.0f)
        : splitGap;

    const float handleRadius = 7.0f;
    const ImVec2 handleCenter(
        std::clamp(
            workspacePos.x + m_LeftPaneWidth + (effectiveSplitGap * 0.5f),
            workspacePos.x + handleRadius + 2.0f,
            workspacePos.x + workspaceSize.x - handleRadius - 2.0f),
        workspacePos.y + 14.0f);
    bool handleHovered = false;
    if (m_SplitAutoAnimating) {
        const float t = static_cast<float>(std::clamp((ImGui::GetTime() - m_SplitAutoAnimStartTime) / 0.2, 0.0, 1.0));
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
        m_LeftPaneWidth = m_SplitAutoAnimFrom + (m_SplitAutoAnimTo - m_SplitAutoAnimFrom) * eased;
        if (t >= 1.0f) {
            m_SplitAutoAnimating = false;
            m_CompositeEdgeSnapMode = m_SplitAutoAnimSnapMode;
        }
    }

    ImDrawList* rootDrawList = ImGui::GetForegroundDrawList();
    const ImU32 handleFill = (m_DraggingSplitHandle || m_SplitHandlePressed)
        ? IM_COL32(92, 178, 255, 230)
        : (handleHovered ? IM_COL32(72, 148, 214, 214) : IM_COL32(52, 92, 120, 188));
    if (m_LibraryLoadGraphRevealAlpha > 0.01f) {
        ImVec4 handleColor = ImGui::ColorConvertU32ToFloat4(handleFill);
        handleColor.w *= m_LibraryLoadGraphRevealAlpha;
        rootDrawList->AddCircleFilled(handleCenter, handleRadius, ImGui::ColorConvertFloat4ToU32(handleColor), 32);
    }

    const float paneHeight = std::max(0.0f, workspaceSize.y);
    const float rightWidth = std::max(0.0f, workspaceSize.x - m_LeftPaneWidth - effectiveSplitGap);

    // Update Left Panel Hover & Animation State
    const bool isNodeBrowserOpen = m_Sidebar.GetNodeGraphUI().IsNodeBrowserOpen();
    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph && m_LeftPaneWidth > 1.0f) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool hoveringPanelOrTab = false;
        
        if (!isNodeBrowserOpen) {
            if (!m_LeftPanelExpanded) {
                // Hover trigger along the entire left wall/edge of the window
                if (mousePos.x >= workspacePos.x && mousePos.x <= workspacePos.x + 15.0f &&
                    mousePos.y >= workspacePos.y && mousePos.y <= workspacePos.y + paneHeight) {
                    hoveringPanelOrTab = true;
                }
            } else {
                if (mousePos.x >= workspacePos.x && mousePos.x <= workspacePos.x + m_LeftPanelWidthAnim + 15.0f &&
                    mousePos.y >= workspacePos.y && mousePos.y <= workspacePos.y + paneHeight) {
                    hoveringPanelOrTab = true;
                }
            }
            
            if (ImGui::IsDragDropActive()) {
                hoveringPanelOrTab = true;
            }
        }
        
        m_LeftPanelExpanded = hoveringPanelOrTab;
    } else {
        m_LeftPanelExpanded = false;
    }

    float leftPanelTargetWidth = m_LeftPanelExpanded ? 220.0f : 0.0f;
    float animDt = ImGui::GetIO().DeltaTime;
    m_LeftPanelWidthAnim += (leftPanelTargetWidth - m_LeftPanelWidthAnim) * animDt * 10.0f;
    if (std::abs(m_LeftPanelWidthAnim - leftPanelTargetWidth) < 0.1f) {
        m_LeftPanelWidthAnim = leftPanelTargetWidth;
    }

    float nodesPanelTargetWidth = isNodeBrowserOpen ? 280.0f : 0.0f;
    m_NodesPanelWidthAnim += (nodesPanelTargetWidth - m_NodesPanelWidthAnim) * animDt * 10.0f;
    if (std::abs(m_NodesPanelWidthAnim - nodesPanelTargetWidth) < 0.1f) {
        m_NodesPanelWidthAnim = nodesPanelTargetWidth;
    }

    if (m_LeftPaneWidth > 1.0f) {
        ImGui::SetCursorScreenPos(workspacePos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_LibraryLoadGraphRevealAlpha);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
        ImGui::BeginChild("EditorGraphPane", ImVec2(m_LeftPaneWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_SubWindowTransitionAlpha);
        m_Sidebar.Render(this);
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_LibraryLoadToolbarRevealAlpha);
        RenderFloatingToolbar();
        ImGui::PopStyleVar();

        // Canvas Stack Slide-out Drawer Overlay
        if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // 1. Sliding panel background & content
            if (m_LeftPanelWidthAnim > 0.1f) {
                ImVec2 panelMin = workspacePos;
                
                // Redesigned premium feathered blend background
                float gradientWidth = std::min(60.0f, m_LeftPanelWidthAnim);
                float solidWidth = m_LeftPanelWidthAnim - gradientWidth;
                
                // Dynamic matching color with translucency based on theme workspace color
                const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
                const bool isLightBg = luminance >= 0.5f;

                ImVec4 colBgOpaqueVec = workspaceColor;
                colBgOpaqueVec.w = isLightBg ? 0.94f : 0.92f;
                const ImU32 colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);

                ImVec4 colBgTransVec = workspaceColor;
                colBgTransVec.w = 0.0f;
                const ImU32 colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);

                const ImU32 colTitleText = isLightBg ? IM_COL32(18, 24, 30, 255) : IM_COL32(255, 255, 255, 255);
                const ImU32 colPassiveText = isLightBg ? IM_COL32(80, 95, 105, 220) : IM_COL32(140, 160, 170, 200);
                const ImU32 colActiveText = isLightBg ? IM_COL32(16, 110, 190, 255) : IM_COL32(92, 178, 255, 255);
                const ImU32 colNormalText = isLightBg ? IM_COL32(40, 50, 60, 220) : IM_COL32(200, 210, 220, 200);
                const ImU32 colHoveredHeader = isLightBg ? IM_COL32(16, 110, 190, 28) : IM_COL32(92, 178, 255, 30);
                const ImU32 colActiveHeader = isLightBg ? IM_COL32(16, 110, 190, 48) : IM_COL32(92, 178, 255, 50);
                
                // Solid part
                if (solidWidth > 0.0f) {
                    drawList->AddRectFilled(panelMin, ImVec2(workspacePos.x + solidWidth, workspacePos.y + paneHeight), colBgOpaque);
                }
                // Gradient feathered blend part
                drawList->AddRectFilledMultiColor(
                    ImVec2(workspacePos.x + solidWidth, workspacePos.y),
                    ImVec2(workspacePos.x + m_LeftPanelWidthAnim, workspacePos.y + paneHeight),
                    colBgOpaque, colBgTrans, colBgTrans, colBgOpaque
                );
                
                // Content area
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_SubWindowTransitionAlpha);
                
                float contentWidth = m_LeftPanelWidthAnim - 40.0f; // breathing room on right for the feathered edge
                if (contentWidth > 1.0f) {
                    ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + 16.0f, workspacePos.y + 28.0f));
                    ImGui::BeginChild("CanvasStackDrawer", ImVec2(contentWidth, paneHeight - 56.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
                    ImGui::TextUnformatted("CANVAS STACK");
                    ImGui::PopStyleColor();
                    
                    ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Spacing instead of solid separator line
                    
                    const std::vector<int>& zOrder = GetCompositeZOrder();
                    if (zOrder.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, colPassiveText);
                        ImGui::TextWrapped("Add at least two completed chains to enable the canvas stack.");
                        ImGui::PopStyleColor();
                    } else {
                        // Keyboard Z-order Reordering via Up/Down Arrow keys
                        const bool drawerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                        const bool drawerHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
                        int selectedId = GetCompositeSelectedOutputNodeId();
                        if ((drawerFocused || drawerHovered) && selectedId > 0 && !ImGui::GetIO().WantTextInput) {
                            int selectedIdx = -1;
                            for (int idx = 0; idx < static_cast<int>(zOrder.size()); ++idx) {
                                if (zOrder[idx] == selectedId) {
                                    selectedIdx = idx;
                                    break;
                                }
                            }
                            if (selectedIdx != -1) {
                                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                                    if (selectedIdx < static_cast<int>(zOrder.size()) - 1) {
                                        MoveCompositeOutputToIndex(selectedId, selectedIdx + 1);
                                        MarkDirty();
                                    }
                                } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                                    if (selectedIdx > 0) {
                                        MoveCompositeOutputToIndex(selectedId, selectedIdx - 1);
                                        MarkDirty();
                                    }
                                }
                            }
                        }

                        ImGui::PushStyleColor(ImGuiCol_Text, colPassiveText);
                        ImGui::TextUnformatted("Drag to reorder:");
                        ImGui::PopStyleColor();
                        ImGui::Dummy(ImVec2(0.0f, 8.0f));
                        
                        // Iterate in REVERSE order (top is front, bottom is back)
                        for (int i = (int)zOrder.size() - 1; i >= 0; --i) {
                            int outputNodeId = zOrder[i];
                            const CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
                            std::string label = item && !item->label.empty() ? item->label : ("Output " + std::to_string(outputNodeId));
                            
                            // Truncate if too long
                            std::string displayLabel = label;
                            if (displayLabel.size() > 18) {
                                displayLabel = displayLabel.substr(0, 15) + "...";
                            }
                            
                            char itemID[128];
                            snprintf(itemID, sizeof(itemID), "%s##DrawerZItem_%d", displayLabel.c_str(), outputNodeId);
                            
                            bool selected = GetCompositeSelectedOutputNodeId() == outputNodeId;
                            
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                            // Completely borderless and backgroundless selectables
                            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colHoveredHeader);
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colActiveHeader);
                            
                            if (selected) {
                                ImGui::PushStyleColor(ImGuiCol_Text, colActiveText); // Active text glow
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, colNormalText); // Floating passive text
                            }
                            
                            if (ImGui::Selectable(itemID, selected, 0, ImVec2(contentWidth, 26.0f))) {
                                SetCompositeSelectedOutputNodeId(outputNodeId);
                            }
                            
                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar();
                            
                            // Drag and Drop Source
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                ImGui::SetDragDropPayload("CompositeZOrderItem", &outputNodeId, sizeof(outputNodeId));
                                ImGui::Text("Moving %s", label.c_str());
                                ImGui::EndDragDropSource();
                            }
                            
                            // Drag and Drop Target
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CompositeZOrderItem")) {
                                    int draggedId = *static_cast<const int*>(payload->Data);
                                    MoveCompositeOutputZOrder(draggedId, outputNodeId);
                                }
                                ImGui::EndDragDropTarget();
                            }
                            
                            ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        }
                    }
                    
                    ImGui::EndChild();
                }
                
                ImGui::PopStyleVar();
            }
        }

        // Nodes Panel Slide-out Drawer Overlay
        if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
            if (m_NodesPanelWidthAnim > 0.1f) {
                m_Sidebar.GetNodeGraphUI().RenderNodesPanelDrawer(this, m_NodesPanelWidthAnim, paneHeight, workspacePos);
            }
        }

        ImGui::EndChild();
        const ImVec2 graphPaneMin = ImGui::GetItemRectMin();
        const ImVec2 graphPaneMax = ImGui::GetItemRectMax();
        if (m_LibraryLoadGraphRevealAlpha < 0.999f) {
            ImVec4 maskColor = workspaceColor;
            maskColor.w = std::clamp(1.0f - m_LibraryLoadGraphRevealAlpha, 0.0f, 1.0f);
            ImGui::GetForegroundDrawList()->AddRectFilled(
                graphPaneMin,
                graphPaneMax,
                ImGui::ColorConvertFloat4ToU32(maskColor));
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    const float revealedRightWidth = rightWidth * m_LibraryLoadCanvasRevealAlpha;
    if (revealedRightWidth > 1.0f) {
        const float viewportX = workspacePos.x + workspaceSize.x - revealedRightWidth;
        ImGui::SetCursorScreenPos(ImVec2(viewportX, workspacePos.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_LibraryLoadCanvasRevealAlpha);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
        ImGui::BeginChild("EditorViewportPane", ImVec2(revealedRightWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        m_Viewport.Render(this, m_LibraryLoadCanvasRevealAlpha);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    ImVec2 handleOverlayPos(handleCenter.x - 14.0f, workspacePos.y);
    ImVec2 handleOverlaySize(32.0f, paneHeight);
    if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly || m_LeftPaneWidth <= 2.0f) {
        handleOverlayPos = ImVec2(workspacePos.x, workspacePos.y);
        handleOverlaySize = ImVec2(32.0f, paneHeight);
    } else if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly || m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
        handleOverlayPos = ImVec2(workspacePos.x + workspaceSize.x - 32.0f, workspacePos.y);
        handleOverlaySize = ImVec2(32.0f, paneHeight);
    }

    ImGui::SetCursorScreenPos(handleOverlayPos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("EditorSplitHandleOverlay", handleOverlaySize, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    ImGui::SetCursorScreenPos(handleOverlayPos);
    ImGui::InvisibleButton("EditorSplitHandle", handleOverlaySize);
    handleHovered = ImGui::IsItemHovered();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (handleHovered || m_DraggingSplitHandle || m_SplitHandlePressed) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (handleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_SplitHandlePressed = true;
        m_SplitHandleMoved = false;
        m_SplitAutoAnimating = false;
    }
    if (m_SplitHandlePressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x) > 2.0f) {
            m_DraggingSplitHandle = true;
            m_SplitHandleMoved = true;
            m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
        }
    }

    if (m_DraggingSplitHandle) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
            if (compositeViewportMode) {
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, 0.0f, workspaceSize.x);
            } else {
                m_NodeGraphFullscreen = false;
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, minLeftWidth, maxLeftWidth);
            }
            if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
                m_LastUserNodeGraphWidth = m_LeftPaneWidth;
            }
        } else {
            m_DraggingSplitHandle = false;
        }
    }
    if (m_SplitHandlePressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!m_SplitHandleMoved) {
            const float leftTarget = compositeViewportMode ? 0.0f : minLeftWidth;
            const float rightTarget = compositeViewportMode ? workspaceSize.x : maxLeftWidth;
            const float centerThreshold = workspaceSize.x * 0.5f;
            const float currentCenter = m_LeftPaneWidth + effectiveSplitGap * 0.5f;
            m_SplitAutoAnimFrom = m_LeftPaneWidth;
            if (compositeViewportMode && m_LeftPaneWidth <= 2.0f) {
                m_SplitAutoAnimTo = rightTarget;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::GraphOnly;
            } else if (compositeViewportMode && m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
                m_SplitAutoAnimTo = leftTarget;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::ViewportOnly;
            } else {
                m_SplitAutoAnimTo = currentCenter < centerThreshold ? leftTarget : rightTarget;
                if (compositeViewportMode) {
                    m_SplitAutoAnimSnapMode = m_SplitAutoAnimTo <= 0.0f
                        ? CompositeEdgeSnapMode::ViewportOnly
                        : CompositeEdgeSnapMode::GraphOnly;
                } else {
                    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
                }
            }
            m_SplitAutoAnimStartTime = ImGui::GetTime();
            m_SplitAutoAnimating = true;
        }
        m_SplitHandlePressed = false;
        m_SplitHandleMoved = false;
    }

    SubmitRenderIfReady();
    RenderProjectLifecyclePopups();

    if (IsGraphDropImportBusy()) {
        ImGuiExtras::RenderBusyOverlay(
            GetGraphDropImportStatusText().empty()
                ? "Importing images into graph..."
                : GetGraphDropImportStatusText().c_str());
    } else if (IsSourceLoadBusy()) {
        ImGuiExtras::RenderBusyOverlay("Loading source image...");
    } else if (IsExportBusy()) {
        ImGuiExtras::RenderBusyOverlay(GetExportStatusText().empty() ? "Exporting..." : GetExportStatusText().c_str());
    } else if (IsEditorRenderBusy()) {
        EditorRenderWorker::RenderProgress progress = m_RenderWorker.GetProgress();
        std::string label = progress.label.empty() ? std::string("Rendering...") : progress.label;
        if (!progress.busy && m_RenderPending) {
            label = "Finalizing render...";
        }
        const int totalSteps = std::max(1, progress.totalSteps);
        float fraction = static_cast<float>(std::clamp(progress.completedSteps, 0, totalSteps)) /
            static_cast<float>(totalSteps);
        if (!progress.busy && m_RenderPending) {
            fraction = std::max(fraction, 0.98f);
        }
        ImGuiExtras::RenderProgressOverlay(label.c_str(), fraction);
    }

    ImGui::EndChild();
}

EditorModule::ViewportMode EditorModule::GetViewportMode() const {
    RefreshCompletedChainCacheIfNeeded();
    return GetCompletedChainCount() >= 2
        ? ViewportMode::CompositeCanvas
        : ViewportMode::SingleOutputPreview;
}

int EditorModule::GetCompletedChainCount() const {
    RefreshCompletedChainCacheIfNeeded();
    return static_cast<int>(m_CachedCompletedChains.size());
}

int EditorModule::GetConnectedOutputCount() const {
    RefreshCompletedChainCacheIfNeeded();
    return m_CachedConnectedOutputCount;
}

bool EditorModule::CanToggleActiveAutoGainMaskPreview() const {
    if (GetViewportMode() != ViewportMode::SingleOutputPreview ||
        m_ActiveSubWindow != EditorSubWindow::ComplexNode) {
        return false;
    }
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_ActiveComplexNodeId);
    return node && node->kind == EditorNodeGraph::NodeKind::RawDetailFusion;
}

void EditorModule::ToggleActiveAutoGainMaskPreview() {
    if (!CanToggleActiveAutoGainMaskPreview()) {
        ClearAutoGainMaskPreview();
        return;
    }
    const int nodeId = m_ActiveComplexNodeId;
    m_AutoGainMaskPreviewNodeId = (m_AutoGainMaskPreviewNodeId == nodeId) ? -1 : nodeId;
    m_RenderDirty = true;
    ++m_RenderRevision;
    m_LastRenderDirtyTime = ImGui::GetTime();
}

void EditorModule::ClearAutoGainMaskPreview() {
    if (m_AutoGainMaskPreviewNodeId <= 0) {
        return;
    }
    m_AutoGainMaskPreviewNodeId = -1;
    m_RenderDirty = true;
    ++m_RenderRevision;
    m_LastRenderDirtyTime = ImGui::GetTime();
}

std::uint64_t EditorModule::GetPreviewNodeRevision(int previewNodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(previewNodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        const EditorNodeGraph::Link* input =
            m_NodeGraph.FindInputLink(previewNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            return 0;
        }
        return std::max<std::uint64_t>(
            1,
            std::max(GetNodeDirtyGeneration(previewNodeId), GetNodeDirtyGeneration(input->fromNodeId)));
    }
    const EditorNodeGraph::Link* input =
        m_NodeGraph.FindAnyInputLink(previewNodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return 0;
    }
    return std::max<std::uint64_t>(1, GetNodeDirtyGeneration(input->fromNodeId));
}

const EditorModule::GraphPreviewPixels* EditorModule::GetCachedPreviewPixelsForNode(int previewNodeId) const {
    const auto it = m_PreviewPixelCache.find(previewNodeId);
    return it != m_PreviewPixelCache.end() ? &it->second : nullptr;
}

std::uint64_t EditorModule::GetScopeNodeRevision(int sourceNodeId) const {
    if (sourceNodeId <= 0) {
        m_ScopeDisplayedRevisions[sourceNodeId] = 0;
        return 0;
    }

    const std::uint64_t desiredRevision = GetNodeDirtyGeneration(sourceNodeId);
    std::uint64_t& displayedRevision = m_ScopeDisplayedRevisions[sourceNodeId];
    if (CanRefreshPreviewLikeNodes() && !HasPendingPreviewRefreshes()) {
        displayedRevision = desiredRevision;
    }
    return displayedRevision;
}

ImVec4 EditorModule::GetWorkspaceBaseColor() const {
    if (m_Appearance) {
        return m_Appearance->GetWorkingTheme().colors[ImGuiCol_WindowBg];
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    }
    return kEditorWorkspaceBaseColor;
}

bool EditorModule::CanConsumeEditorCommandKeys() const {
    return !ImGui::GetIO().WantTextInput;
}

void EditorModule::SwitchToSubWindow(EditorSubWindow target) {
    if (m_ActiveSubWindow == target && m_TargetSubWindow == target) {
        return;
    }
    if (target != EditorSubWindow::NodeGraph) {
        m_NodeGraphFullscreen = false;
    }
    m_TargetSubWindow = target;
    if (target != EditorSubWindow::ComplexNode) {
        m_TargetComplexNodeId = -1;
    }
    m_SubWindowTransitionFadingOut = true;
}

void EditorModule::SwitchToComplexNodeSubWindow(int nodeId) {
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode && m_ActiveComplexNodeId == nodeId &&
        m_TargetSubWindow == EditorSubWindow::ComplexNode && m_TargetComplexNodeId == nodeId) {
        return;
    }
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        m_TargetSubWindow == EditorSubWindow::ComplexNode &&
        !m_SubWindowTransitionFadingOut &&
        m_SubWindowTransitionAlpha >= 0.999f) {
        m_ActiveComplexNodeId = nodeId;
        m_TargetComplexNodeId = nodeId;
        return;
    }
    m_NodeGraphFullscreen = false;
    m_TargetSubWindow = EditorSubWindow::ComplexNode;
    m_TargetComplexNodeId = nodeId;
    m_SubWindowTransitionFadingOut = true;
}

static unsigned int LoadEditorResourceTexture(const unsigned char* data, unsigned int size, const char* debugName) {
    if (!data || size == 0) {
        return 0;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[EditorModule] Failed to decode embedded %s icon.\n", debugName);
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

static unsigned int LoadTextureFromFile(const char* path, const char* debugName) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[EditorModule] Failed to load %s icon from path: %s\n", debugName, path);
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

void EditorModule::LoadResourceTextures() {
    if (m_TexturesLoaded) {
        return;
    }
    m_NodeGraphIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::NodeGraph_png_data,
        EmbeddedTabIcons::NodeGraph_png_size,
        "NodeGraph"
    );
    m_ExportIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Export_png_data,
        EmbeddedTabIcons::Export_png_size,
        "Export"
    );
    m_SettingsIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Settings_png_data,
        EmbeddedTabIcons::Settings_png_size,
        "Settings"
    );
    m_BackgroundRemoverIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::BackgroundRemover_png_data,
        EmbeddedTabIcons::BackgroundRemover_png_size,
        "BackgroundRemover"
    );
    m_ColorGradeIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::ColorGrade_png_data,
        EmbeddedTabIcons::ColorGrade_png_size,
        "ColorGrade"
    );
    m_TexturesLoaded = true;
}

void EditorModule::RenderFloatingToolbar() {
    LoadResourceTextures();

    const float radius = 20.0f;
    const float margin = 24.0f;
    const float spacing = 12.0f;
    const float buttonDiameter = radius * 2.0f;

    // Get the position and size of the parent child window (EditorGraphPane)
    const ImVec2 parentPos = ImGui::GetWindowPos();
    const ImVec2 parentSize = ImGui::GetWindowSize();

    const ImVec2 basePos(
        parentPos.x + margin + radius + m_LeftPanelWidthAnim,
        parentPos.y + parentSize.y - margin - radius
    );

    struct ToolbarButton {
        bool isStandard = true;
        EditorSubWindow subWin = EditorSubWindow::NodeGraph;
        int nodeId = -1;
        std::string typeId;
        std::string label;
    };

    std::vector<ToolbarButton> buttons;
    buttons.push_back({ true, EditorSubWindow::Settings, -1, "", "Editor Settings [3]" });
    buttons.push_back({ true, EditorSubWindow::NodeGraph, -1, "", "Node Graph [1]" });

    // Append dynamic complex node buttons
    for (const auto& node : m_NodeGraph.GetNodes()) {
        if (!NodeUsesRichNodeSurface(node.id)) {
            continue;
        }
        const NodeSurfaceSpec surfaceSpec = GetNodeSurfaceSpec(node.id);
        if (surfaceSpec.presentation != NodeSurfacePresentation::RichExpandedSurface) {
            continue;
        }
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node.typeId);
        const std::string label = !node.title.empty()
            ? node.title
            : (descriptor && descriptor->displayName ? descriptor->displayName : std::string("Advanced Node"));
        buttons.push_back({ false, EditorSubWindow::ComplexNode, node.id, node.typeId, label });
    }

    if (GetCompletedChainCount() >= 2) {
        buttons.push_back({ true, EditorSubWindow::ExportSettings, -1, "", "Export Settings" });
    }

    // Calculate window size to encompass all buttons plus shadow padding
    const float toolbarWidth = buttons.size() * (buttonDiameter + spacing) - spacing + radius * 2.0f;
    const float toolbarHeight = buttonDiameter + 8.0f; // Add a small vertical pad for shadow / border
    const ImVec2 toolbarPos(basePos.x - radius - 4.0f, basePos.y - radius - 4.0f);

    ImGui::SetNextWindowPos(toolbarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbarWidth + 8.0f, toolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(10.0f, 10.0f));

    ImGui::Begin("##EditorFloatingToolbar", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    auto scaleAlpha = [](ImU32 col, float alpha) -> ImU32 {
        unsigned int r = col & 0xFF;
        unsigned int g = (col >> 8) & 0xFF;
        unsigned int b = (col >> 16) & 0xFF;
        unsigned int a = (col >> 24) & 0xFF;
        unsigned int newA = static_cast<unsigned int>(a * alpha);
        return r | (g << 8) | (b << 16) | (newA << 24);
    };

    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& btn = buttons[i];
        const bool isActive = btn.isStandard 
            ? (m_TargetSubWindow == btn.subWin) 
            : (m_TargetSubWindow == EditorSubWindow::ComplexNode && m_TargetComplexNodeId == btn.nodeId);
        
        ImVec2 center(basePos.x + i * (buttonDiameter + spacing), basePos.y);

        float animAlpha = 1.0f;
        if (!btn.isStandard) {
            double curTime = ImGui::GetTime();
            double spawnTime = curTime;
            auto it = m_ToolbarButtonSpawnTimes.find(btn.nodeId);
            if (it == m_ToolbarButtonSpawnTimes.end()) {
                m_ToolbarButtonSpawnTimes[btn.nodeId] = curTime;
            } else {
                spawnTime = it->second;
            }
            double age = curTime - spawnTime;
            float tAnim = std::clamp(static_cast<float>(age / 0.4), 0.0f, 1.0f);
            float easeOut = 1.0f - std::pow(1.0f - tAnim, 3.0f);

            center.x += (easeOut - 1.0f) * 40.0f;
            animAlpha = easeOut;
        }
        animAlpha *= m_LibraryLoadToolbarRevealAlpha;

        ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
        char btnId[64];
        if (btn.isStandard) {
            snprintf(btnId, sizeof(btnId), "##FloatingSubWindowBtn_%d", static_cast<int>(btn.subWin));
        } else {
            snprintf(btnId, sizeof(btnId), "##FloatingComplexNodeBtn_%d", btn.nodeId);
        }
        
        ImGui::InvisibleButton(btnId, ImVec2(buttonDiameter, buttonDiameter));
        const bool nonModalPopupOpen =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            !HasBlockingModalPopupOpen();
        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        const float dx = mousePos.x - center.x;
        const float dy = mousePos.y - center.y;
        const bool manualHover = nonModalPopupOpen && (dx * dx + dy * dy) <= (radius * radius);
        const bool hovered = ImGui::IsItemHovered() || manualHover;
        const bool clicked = ImGui::IsItemClicked() || (manualHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left));

        if (clicked) {
            CloseNonModalPopupsForToolbarSwitch();
            if (btn.isStandard) {
                SwitchToSubWindow(btn.subWin);
            } else {
                SwitchToComplexNodeSubWindow(btn.nodeId);
            }
        }

        unsigned int tex = 0;
        if (btn.isStandard) {
            if (btn.subWin == EditorSubWindow::NodeGraph) {
                tex = m_NodeGraphIconTexture;
            } else if (btn.subWin == EditorSubWindow::ExportSettings) {
                tex = m_ExportIconTexture;
            } else if (btn.subWin == EditorSubWindow::Settings) {
                tex = m_SettingsIconTexture;
            }
        } else {
            if (btn.typeId == "BackgroundPatcher") {
                tex = m_BackgroundRemoverIconTexture;
            } else if (btn.typeId == "ColorGrade") {
                tex = m_ColorGradeIconTexture;
            } else {
                tex = m_SettingsIconTexture;
            }
        }

        if (tex) {
            const float iconHalfSize = 12.0f;
            ImVec2 pMin(center.x - iconHalfSize, center.y - iconHalfSize);
            ImVec2 pMax(center.x + iconHalfSize, center.y + iconHalfSize);
            
            ImU32 iconColor = (isActive || ImGui::IsItemActive())
                ? IM_COL32(255, 255, 255, 255)
                : (hovered ? IM_COL32(220, 220, 220, 230) : IM_COL32(150, 150, 150, 165));
            if (animAlpha < 1.0f) {
                iconColor = scaleAlpha(iconColor, animAlpha);
            }
            drawList->AddImage((ImTextureID)(intptr_t)tex, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), iconColor);
        }

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(btn.label.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);

    // Clean up spawn times map
    for (auto it = m_ToolbarButtonSpawnTimes.begin(); it != m_ToolbarButtonSpawnTimes.end();) {
        bool found = false;
        for (const auto& btn : buttons) {
            if (!btn.isStandard && btn.nodeId == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_ToolbarButtonSpawnTimes.erase(it);
        } else {
            ++it;
        }
    }
}
