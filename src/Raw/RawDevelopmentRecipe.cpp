#include "Raw/RawDevelopmentRecipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_set>

namespace Stack::RawRecipe {
namespace {

constexpr std::size_t kMaxRawToneCurvePoints = 12;
constexpr std::size_t kMaxRawLocalRangePoints = 12;

std::vector<RawToneCurvePoint> DefaultToneCurvePoints() {
    return {
        RawToneCurvePoint{ 0.0f, 0.0f },
        RawToneCurvePoint{ 1.0f, 1.0f }
    };
}

nlohmann::json DefaultToneCurveLayerPointsJson() {
    return nlohmann::json::array({
        {
            { "x", 0.0f },
            { "y", 0.0f },
            { "shape", 1 }
        },
        {
            { "x", 1.0f },
            { "y", 1.0f },
            { "shape", 1 }
        }
    });
}

float ClampFinite(float value, float fallback, float minValue, float maxValue) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minValue, maxValue);
}

Raw::RawMosaicDenoiseSettings SanitizeMosaicDenoiseSettings(
    Raw::RawMosaicDenoiseSettings settings) {
    const Raw::RawMosaicDenoiseSettings defaults;
    if (settings.mode != Raw::RawMosaicDenoiseMode::LegacyFixedThreshold &&
        settings.mode != Raw::RawMosaicDenoiseMode::DngNoiseProfile) {
        settings.mode = defaults.mode;
    }
    settings.hotPixelThreshold = ClampFinite(
        settings.hotPixelThreshold,
        defaults.hotPixelThreshold,
        0.001f,
        1.0f);
    settings.lumaStrength = ClampFinite(
        settings.lumaStrength,
        defaults.lumaStrength,
        0.0f,
        1.0f);
    settings.chromaStrength = ClampFinite(
        settings.chromaStrength,
        defaults.chromaStrength,
        0.0f,
        1.0f);
    settings.radius = std::clamp(settings.radius, 1, 4);
    settings.edgeProtection = ClampFinite(
        settings.edgeProtection,
        defaults.edgeProtection,
        0.0f,
        1.0f);
    settings.iterations = std::clamp(settings.iterations, 1, 2);
    return settings;
}

float SmoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float LocalRangeEdgeAwareWeight(float evDifference, const RawLocalRangeRecipe& localRange) {
    const float diff = std::max(0.0f, std::abs(evDifference));
    const float edgeProtection = std::clamp(localRange.edgeProtection, 0.0f, 1.0f);
    const float detailProtection = std::clamp(localRange.detailProtection, 0.0f, 1.0f);
    const float sigma = std::max(0.05f, 2.40f + (0.45f - 2.40f) * edgeProtection);
    float rangeWeight = std::exp(-(diff * diff) / (2.0f * sigma * sigma));
    const float textureLow = 0.12f + (0.35f - 0.12f) * detailProtection;
    const float textureHigh = 0.55f + (1.25f - 0.55f) * detailProtection;
    const float textureInclusion =
        1.0f - SmoothStep(textureLow, textureHigh, diff);
    rangeWeight = std::max(rangeWeight, textureInclusion * detailProtection);
    return (1.0f - edgeProtection) + edgeProtection * std::clamp(rangeWeight, 0.0f, 1.0f);
}

bool IsIdentityToneCurve(const RawToneCurveRecipe& toneCurve) {
    if (toneCurve.points.empty()) {
        return true;
    }
    for (const RawToneCurvePoint& point : toneCurve.points) {
        if (!std::isfinite(point.input) ||
            !std::isfinite(point.output) ||
            std::abs(std::clamp(point.input, 0.0f, 1.0f) - std::clamp(point.output, 0.0f, 1.0f)) > 0.0001f) {
            return false;
        }
    }
    return true;
}

nlohmann::json SanitizeFinishToneJson(
    nlohmann::json value,
    const RawToneCurveRecipe& legacyToneCurve,
    int storedRecipeVersion) {
    if (!value.is_object()) {
        value = FinishToneJsonFromLegacyToneCurve(legacyToneCurve);
    }
    value["type"] = "ToneCurve";
    value["mode"] = std::clamp(value.value("mode", 1), 0, 4);
    value["domain"] = std::clamp(value.value("domain", 1), 0, 1);
    value["activeGraphView"] = std::clamp(value.value("activeGraphView", 0), 0, 1);
    value["logMinEv"] = ClampFinite(value.value("logMinEv", -10.0f), -10.0f, -20.0f, 0.0f);
    value["logMaxEv"] = ClampFinite(value.value("logMaxEv", 6.0f), 6.0f, 0.0f, 20.0f);
    value["middleGrey"] = ClampFinite(value.value("middleGrey", 0.18f), 0.18f, 0.01f, 1.0f);
    if (value["logMaxEv"].get<float>() <= value["logMinEv"].get<float>() + 0.1f) {
        value["logMaxEv"] = value["logMinEv"].get<float>() + 0.1f;
    }
    if (!value.contains("points") || !value["points"].is_array() || value["points"].size() < 2) {
        value["points"] = DefaultToneCurveLayerPointsJson();
    }
    if (!value.contains("preparedPoints") || !value["preparedPoints"].is_array() || value["preparedPoints"].size() < 2) {
        value["preparedPoints"] = value["points"];
    }
    return SanitizeFinishTonePointCurveJson(std::move(value), storedRecipeVersion);
}

nlohmann::json SanitizeViewTransformJson(nlohmann::json value) {
    if (!value.is_object()) {
        value = DefaultViewTransformJson();
    }
    value["type"] = "ViewTransform";
    value["enabled"] =
        value.contains("enabled") && value["enabled"].is_boolean()
            ? value["enabled"].get<bool>()
            : true;
    value["exposure"] = ClampFinite(value.value("exposure", 0.0f), 0.0f, -8.0f, 8.0f);
    value["blackEv"] = ClampFinite(value.value("blackEv", -8.0f), -8.0f, -16.0f, 0.0f);
    value["whiteEv"] = ClampFinite(value.value("whiteEv", 4.0f), 4.0f, 0.0f, 16.0f);
    value["middleGrey"] = ClampFinite(value.value("middleGrey", 0.18f), 0.18f, 0.01f, 1.0f);
    value["shoulder"] = ClampFinite(value.value("shoulder", 0.45f), 0.45f, 0.05f, 4.0f);
    value["toe"] = ClampFinite(value.value("toe", 0.18f), 0.18f, 0.0f, 1.0f);
    value["contrast"] = ClampFinite(value.value("contrast", 1.0f), 1.0f, 0.25f, 2.5f);
    value["saturation"] = ClampFinite(value.value("saturation", 1.0f), 1.0f, 0.0f, 2.0f);
    value["preserveHue"] = value.value("preserveHue", true);
    value["debugFalseColor"] = value.value("debugFalseColor", false);
    value["inputWorkingSpace"] = value.value("inputWorkingSpace", std::string("linear-rec2020-d65"));
    value["encodeSrgbOutput"] = value.value("encodeSrgbOutput", true);
    return value;
}

RawLocalExposureRecipe SanitizeLocalExposureRecipe(RawLocalExposureRecipe localExposure) {
    RawLocalExposureRecipe defaults;
    localExposure.amount = ClampFinite(localExposure.amount, defaults.amount, 0.0f, 1.0f);
    localExposure.shadowLiftEv = ClampFinite(localExposure.shadowLiftEv, defaults.shadowLiftEv, 0.0f, 4.0f);
    localExposure.highlightCompressionEv = ClampFinite(
        localExposure.highlightCompressionEv,
        defaults.highlightCompressionEv,
        -4.0f,
        0.0f);
    localExposure.localBaselineEv = ClampFinite(localExposure.localBaselineEv, defaults.localBaselineEv, -1.25f, 1.25f);
    localExposure.noiseGuardBias = ClampFinite(localExposure.noiseGuardBias, defaults.noiseGuardBias, -1.0f, 1.0f);
    localExposure.highlightGuardBias = ClampFinite(
        localExposure.highlightGuardBias,
        defaults.highlightGuardBias,
        -1.0f,
        1.0f);
    localExposure.shadowGuardBias = ClampFinite(
        localExposure.shadowGuardBias,
        defaults.shadowGuardBias,
        -1.0f,
        1.0f);
    localExposure.smoothGradientProtection = ClampFinite(
        localExposure.smoothGradientProtection,
        defaults.smoothGradientProtection,
        0.0f,
        1.0f);
    localExposure.haloGuard = ClampFinite(localExposure.haloGuard, defaults.haloGuard, 0.0f, 1.0f);
    return localExposure;
}

bool IsSupportedLocalRangeMaskPreviewMode(const std::string& value) {
    return value == "none" ||
        value == "affected-tones" ||
        value == "delta-map" ||
        value == "region-mask" ||
        value == "before-after";
}

bool IsSupportedLocalRangeRegionMaskMode(const std::string& value) {
    return value == "linear-gradient" ||
        value == "radial-gradient" ||
        value == "luminance-range";
}

float ColorChroma(float r, float g, float b) {
    const float maxChannel = std::max({ r, g, b, 0.0f });
    if (maxChannel <= 0.000001f) {
        return 0.0f;
    }
    const float minChannel = std::min({ std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f) });
    return std::clamp((maxChannel - minChannel) / maxChannel, 0.0f, 1.0f);
}

std::array<float, 3> ColorDirection(float r, float g, float b) {
    const float cr = std::max(r, 0.0f);
    const float cg = std::max(g, 0.0f);
    const float cb = std::max(b, 0.0f);
    const float length = std::sqrt(cr * cr + cg * cg + cb * cb);
    if (length <= 0.000001f) {
        return { 0.57735026f, 0.57735026f, 0.57735026f };
    }
    return { cr / length, cg / length, cb / length };
}

nlohmann::json LocalRangeJson(const RawLocalRangeRecipe& input) {
    const RawLocalRangeRecipe localRange = SanitizeLocalRangeRecipe(input);
    nlohmann::json points = nlohmann::json::array();
    for (const RawLocalRangePoint& point : localRange.points) {
        points.push_back({
            { "ev", point.ev },
            { "deltaEv", point.deltaEv }
        });
    }
    nlohmann::json targetZones = nlohmann::json::array();
    for (const RawLocalRangeTargetZone& zone : localRange.targetZones) {
        nlohmann::json seeds = nlohmann::json::array();
        for (const RawLocalRangeTargetSeed& seed : zone.seeds) {
            seeds.push_back({
                { "sourceU", seed.sourceU },
                { "sourceV", seed.sourceV }
            });
        }
        targetZones.push_back({
            { "id", zone.id },
            { "name", zone.name },
            { "enabled", zone.enabled },
            { "centerEv", zone.centerEv },
            { "coreHalfWidthEv", zone.coreHalfWidthEv },
            { "featherEv", zone.featherEv },
            { "deltaEv", zone.deltaEv },
            { "scope", LocalRangeTargetScopeStableString(zone.scope) },
            { "colorEnabled", zone.colorEnabled },
            { "targetUPrime", zone.targetUPrime },
            { "targetVPrime", zone.targetVPrime },
            { "targetChroma", zone.targetChroma },
            { "colorRadius", zone.colorRadius },
            { "colorFeather", zone.colorFeather },
            { "seeds", std::move(seeds) }
        });
    }
    return {
        { "enabled", localRange.enabled },
        { "strength", localRange.strength },
        { "middleGrey", localRange.middleGrey },
        { "minEv", localRange.minEv },
        { "maxEv", localRange.maxEv },
        { "points", points },
        { "smoothness", localRange.smoothness },
        { "edgeProtection", localRange.edgeProtection },
        { "detailProtection", localRange.detailProtection },
        { "highlightProtection", localRange.highlightProtection },
        { "maskPreviewMode", localRange.maskPreviewMode },
        { "regionMaskEnabled", localRange.regionMaskEnabled },
        { "regionMaskMode", localRange.regionMaskMode },
        { "regionMaskInvert", localRange.regionMaskInvert },
        { "regionMaskCenterX", localRange.regionMaskCenterX },
        { "regionMaskCenterY", localRange.regionMaskCenterY },
        { "regionMaskAngleDegrees", localRange.regionMaskAngleDegrees },
        { "regionMaskSize", localRange.regionMaskSize },
        { "regionMaskFeather", localRange.regionMaskFeather },
        { "regionMaskLowEv", localRange.regionMaskLowEv },
        { "regionMaskHighEv", localRange.regionMaskHighEv },
        { "colorMaskEnabled", localRange.colorMaskEnabled },
        { "colorMaskTargetR", localRange.colorMaskTargetR },
        { "colorMaskTargetG", localRange.colorMaskTargetG },
        { "colorMaskTargetB", localRange.colorMaskTargetB },
        { "colorMaskHueWidth", localRange.colorMaskHueWidth },
        { "colorMaskFeather", localRange.colorMaskFeather },
        { "colorMaskMinChroma", localRange.colorMaskMinChroma },
        { "targetZoneCombineMode",
            LocalRangeZoneCombineModeStableString(localRange.targetZoneCombineMode) },
        { "targetZones", std::move(targetZones) }
    };
}

std::vector<std::string> NormalizeStageOrder(std::vector<std::string> order) {
    if (order.empty()) {
        return DefaultStageOrder();
    }

    auto hasStage = [&](const char* stage) {
        return std::find(order.begin(), order.end(), stage) != order.end();
    };

    auto insertBeforeFirstKnownStage = [&](const char* stage) {
        auto toneIt = std::find(order.begin(), order.end(), "tone-curve");
        if (toneIt != order.end()) {
            order.insert(toneIt, stage);
            return;
        }

        auto viewIt = std::find(order.begin(), order.end(), "view-transform");
        if (viewIt != order.end()) {
            order.insert(viewIt, stage);
            return;
        }

        auto outputIt = std::find(order.begin(), order.end(), "output");
        if (outputIt != order.end()) {
            order.insert(outputIt, stage);
            return;
        }

        order.push_back(stage);
    };

    if (!hasStage("rgb-denoise")) {
        auto exposureIt = std::find(order.begin(), order.end(), "pre-tone-exposure");
        if (exposureIt != order.end()) {
            order.insert(exposureIt, "rgb-denoise");
        } else {
            auto localExposureIt = std::find(order.begin(), order.end(), "local-exposure");
            if (localExposureIt != order.end()) {
                order.insert(localExposureIt, "rgb-denoise");
            } else {
                insertBeforeFirstKnownStage("rgb-denoise");
            }
        }
    }
    if (!hasStage("local-exposure")) {
        auto localRangeIt = std::find(order.begin(), order.end(), "local-range");
        if (localRangeIt != order.end()) {
            order.insert(localRangeIt, "local-exposure");
        } else {
            insertBeforeFirstKnownStage("local-exposure");
        }
    }
    if (!hasStage("local-range")) {
        insertBeforeFirstKnownStage("local-range");
    }
    return order;
}

std::vector<Raw::RawToneCurvePoint> BuildRawToneCurveSettingsPoints(
    const RawToneCurveRecipe& toneCurve) {
    if (toneCurve.mode != ToneCurveMode::Custom) {
        return {};
    }

    std::vector<Raw::RawToneCurvePoint> points;
    points.reserve(toneCurve.points.size() + 2);
    for (const RawToneCurvePoint& point : toneCurve.points) {
        if (!std::isfinite(point.input) || !std::isfinite(point.output)) {
            continue;
        }
        points.push_back({
            std::clamp(point.input, 0.0f, 1.0f),
            std::clamp(point.output, 0.0f, 1.0f)
        });
    }
    points.push_back({ 0.0f, 0.0f });
    points.push_back({ 1.0f, 1.0f });

    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.input < b.input;
    });

    std::vector<Raw::RawToneCurvePoint> uniquePoints;
    uniquePoints.reserve(points.size());
    for (const Raw::RawToneCurvePoint& point : points) {
        if (!uniquePoints.empty() &&
            std::abs(uniquePoints.back().input - point.input) < 0.0001f) {
            uniquePoints.back() = point;
            continue;
        }
        uniquePoints.push_back(point);
    }
    if (uniquePoints.empty()) {
        return {};
    }

    uniquePoints.front() = { 0.0f, 0.0f };
    uniquePoints.back() = { 1.0f, 1.0f };
    while (uniquePoints.size() > kMaxRawToneCurvePoints) {
        uniquePoints.erase(uniquePoints.end() - 2);
    }
    return uniquePoints;
}

std::string FileNameFromPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path parsed(path);
    const std::filesystem::path fileName = parsed.filename();
    if (error) {
        return {};
    }
    const std::string result = fileName.string();
    return result.empty() ? path : result;
}

float JsonFloat(const nlohmann::json& value, const char* key, float fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<float>();
}

int JsonInteger(const nlohmann::json& value, const char* key, int fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<int>();
}

std::uint64_t JsonUInt64(const nlohmann::json& value, const char* key, std::uint64_t fallback) {
    const auto it = value.find(key);
    if (it == value.end()) {
        return fallback;
    }
    if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
    }
    if (it->is_number_integer()) {
        const std::int64_t signedValue = it->get<std::int64_t>();
        return signedValue >= 0
            ? static_cast<std::uint64_t>(signedValue)
            : fallback;
    }
    return fallback;
}

std::int64_t JsonInt64(const nlohmann::json& value, const char* key, std::int64_t fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<std::int64_t>();
}

} // namespace

const std::vector<std::string>& DefaultStageOrder() {
    static const std::vector<std::string> kOrder = {
        "source",
        "raw-decode",
        "white-balance",
        "rgb-denoise",
        "pre-tone-exposure",
        "local-exposure",
        "local-range",
        "tone-curve",
        "view-transform",
        "crop-rotation",
        "output"
    };
    return kOrder;
}

RawDevelopmentRecipe MakeDefaultRecipe(std::string sourcePath, std::string displayName) {
    RawDevelopmentRecipe recipe;
    recipe.source.sourcePath = std::move(sourcePath);
    recipe.source.relativePathKey = recipe.source.sourcePath;
    recipe.source.displayName = displayName.empty() ? FileNameFromPath(recipe.source.sourcePath) : std::move(displayName);
    recipe.localRange = DefaultLocalRangeRecipe();
    recipe.toneCurve.points = DefaultToneCurvePoints();
    recipe.finishTone.layerJson = DefaultFinishToneJson();
    recipe.viewTransform.layerJson = DefaultViewTransformJson();
    recipe.stageOrder = DefaultStageOrder();
    return recipe;
}

const char* WhiteBalanceModeStableString(WhiteBalanceMode mode) {
    switch (mode) {
        case WhiteBalanceMode::AsShot: return "as-shot";
        case WhiteBalanceMode::Auto: return "auto";
        case WhiteBalanceMode::CustomMultipliers: return "custom-multipliers";
        case WhiteBalanceMode::SampledGrayPoint: return "sampled-gray-point";
    }
    return "as-shot";
}

WhiteBalanceMode WhiteBalanceModeFromStableString(const std::string& value) {
    if (value == "auto") {
        return WhiteBalanceMode::Auto;
    }
    if (value == "custom" || value == "custom-multipliers") {
        return WhiteBalanceMode::CustomMultipliers;
    }
    if (value == "sample" || value == "sampled-gray-point") {
        return WhiteBalanceMode::SampledGrayPoint;
    }
    return WhiteBalanceMode::AsShot;
}

const char* ToneCurveModeStableString(ToneCurveMode mode) {
    switch (mode) {
        case ToneCurveMode::Default: return "default";
        case ToneCurveMode::Custom: return "custom";
    }
    return "default";
}

ToneCurveMode ToneCurveModeFromStableString(const std::string& value) {
    if (value == "custom") {
        return ToneCurveMode::Custom;
    }
    return ToneCurveMode::Default;
}

const char* ProcessingVersionStableString(Raw::RawProcessingVersion version) {
    switch (version) {
        case Raw::RawProcessingVersion::LegacyV1: return "legacy-v1";
        case Raw::RawProcessingVersion::TruthfulV1: return "truthful-v1";
    }
    return "legacy-v1";
}

Raw::RawProcessingVersion ProcessingVersionFromStableString(const std::string& value) {
    if (value == "truthful-v1") {
        return Raw::RawProcessingVersion::TruthfulV1;
    }
    return Raw::RawProcessingVersion::LegacyV1;
}

const char* DemosaicMethodStableString(Raw::DemosaicMethod method) {
    switch (method) {
        case Raw::DemosaicMethod::Bilinear: return "bilinear";
        case Raw::DemosaicMethod::MalvarHeCutler: return "malvar-he-cutler-5x5";
    }
    return "bilinear";
}

Raw::DemosaicMethod DemosaicMethodFromStableString(const std::string& value) {
    if (value == "malvar-he-cutler" || value == "malvar-he-cutler-5x5" || value == "mhc") {
        return Raw::DemosaicMethod::MalvarHeCutler;
    }
    return Raw::DemosaicMethod::Bilinear;
}

const char* WorkingSpaceStableString(Raw::RawWorkingSpace workingSpace) {
    switch (workingSpace) {
        case Raw::RawWorkingSpace::LinearSrgbD65: return "linear-srgb-d65";
        case Raw::RawWorkingSpace::LinearRec2020D65: return "linear-rec2020-d65";
    }
    return "linear-srgb-d65";
}

Raw::RawWorkingSpace WorkingSpaceFromStableString(const std::string& value) {
    if (value == "linear-rec2020-d65" || value == "linear-rec2020") {
        return Raw::RawWorkingSpace::LinearRec2020D65;
    }
    return Raw::RawWorkingSpace::LinearSrgbD65;
}

const char* MosaicDenoiseModeStableString(Raw::RawMosaicDenoiseMode mode) {
    switch (mode) {
        case Raw::RawMosaicDenoiseMode::LegacyFixedThreshold:
            return "legacy-fixed-threshold";
        case Raw::RawMosaicDenoiseMode::DngNoiseProfile:
            return "dng-noise-profile-v1";
    }
    return "dng-noise-profile-v1";
}

Raw::RawMosaicDenoiseMode MosaicDenoiseModeFromStableString(
    const std::string& value) {
    if (value == "legacy-fixed-threshold") {
        return Raw::RawMosaicDenoiseMode::LegacyFixedThreshold;
    }
    return Raw::RawMosaicDenoiseMode::DngNoiseProfile;
}

const char* RgbDenoiseMethodStableString(RawRgbDenoiseMethod method) {
    switch (method) {
        case RawRgbDenoiseMethod::ClassicalMultiscaleV1:
            return "classical-multiscale-v1";
        case RawRgbDenoiseMethod::RestormerRealV1:
            return "restormer-real-v1";
        case RawRgbDenoiseMethod::RestormerGaussianBlindV1:
            return "restormer-gaussian-blind-v1";
    }
    return "classical-multiscale-v1";
}

RawRgbDenoiseMethod RgbDenoiseMethodFromStableString(const std::string& value) {
    if (value == "restormer-real-v1") {
        return RawRgbDenoiseMethod::RestormerRealV1;
    }
    if (value == "restormer-gaussian-blind-v1") {
        return RawRgbDenoiseMethod::RestormerGaussianBlindV1;
    }
    return RawRgbDenoiseMethod::ClassicalMultiscaleV1;
}

const char* RgbDenoiseMappingStableString(RawRgbDenoiseMapping mapping) {
    switch (mapping) {
        case RawRgbDenoiseMapping::SceneLinearSafeV1:
            return "scene-linear-safe-v1";
        case RawRgbDenoiseMapping::ProcessedRgbMatchV1:
            return "processed-rgb-match-v1";
    }
    return "scene-linear-safe-v1";
}

RawRgbDenoiseMapping RgbDenoiseMappingFromStableString(const std::string& value) {
    if (value == "processed-rgb-match-v1") {
        return RawRgbDenoiseMapping::ProcessedRgbMatchV1;
    }
    return RawRgbDenoiseMapping::SceneLinearSafeV1;
}

const char* LocalRangeTargetScopeStableString(RawLocalRangeTargetScope scope) {
    switch (scope) {
        case RawLocalRangeTargetScope::SelectedAreas: return "selected-areas";
        case RawLocalRangeTargetScope::AllMatches: return "all-matches";
    }
    return "selected-areas";
}

RawLocalRangeTargetScope LocalRangeTargetScopeFromStableString(const std::string& value) {
    if (value == "all-matches") {
        return RawLocalRangeTargetScope::AllMatches;
    }
    return RawLocalRangeTargetScope::SelectedAreas;
}

const char* LocalRangeZoneCombineModeStableString(RawLocalRangeZoneCombineMode mode) {
    switch (mode) {
        case RawLocalRangeZoneCombineMode::Add: return "add";
        case RawLocalRangeZoneCombineMode::Strongest: return "strongest";
        case RawLocalRangeZoneCombineMode::Blend: return "blend";
    }
    return "add";
}

RawLocalRangeZoneCombineMode LocalRangeZoneCombineModeFromStableString(const std::string& value) {
    if (value == "strongest") {
        return RawLocalRangeZoneCombineMode::Strongest;
    }
    if (value == "blend") {
        return RawLocalRangeZoneCombineMode::Blend;
    }
    return RawLocalRangeZoneCombineMode::Add;
}

namespace {

nlohmann::json IdentityPointCurveJson() {
    return nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f }, { "shape", 1 } },
        { { "x", 1.0f }, { "y", 1.0f }, { "shape", 1 } }
    });
}

std::vector<RawPointCurveControlPoint> SanitizePointCurvePoints(
    const nlohmann::json& value) {
    std::vector<RawPointCurveControlPoint> points;
    if (value.is_array()) {
        points.reserve(std::min<std::size_t>(value.size(), kMaxRawPointCurvePoints));
        for (const nlohmann::json& item : value) {
            if (!item.is_object()) {
                continue;
            }
            RawPointCurveControlPoint point;
            point.x = ClampFinite(JsonFloat(item, "x", 0.0f), 0.0f, 0.0f, 1.0f);
            point.y = ClampFinite(JsonFloat(item, "y", point.x), point.x, 0.0f, 1.0f);
            point.shape = std::clamp(JsonInteger(item, "shape", 1), 0, 2);
            points.push_back(point);
        }
    }
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.x < b.x;
    });
    std::vector<RawPointCurveControlPoint> unique;
    unique.reserve(points.size() + 2u);
    for (const RawPointCurveControlPoint& point : points) {
        if (!unique.empty() && std::abs(unique.back().x - point.x) < 0.0001f) {
            unique.back() = point;
        } else {
            unique.push_back(point);
        }
    }
    if (unique.empty() || unique.front().x > 0.0001f) {
        unique.insert(unique.begin(), { 0.0f, 0.0f, 1 });
    }
    if (unique.size() == 1u || unique.back().x < 0.9999f) {
        unique.push_back({ 1.0f, 1.0f, 1 });
    }
    unique.front().x = 0.0f;
    unique.back().x = 1.0f;
    while (unique.size() > kMaxRawPointCurvePoints) {
        unique.erase(unique.end() - 2);
    }
    return unique;
}

nlohmann::json SerializePointCurvePoints(
    const std::vector<RawPointCurveControlPoint>& points) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawPointCurveControlPoint& point : points) {
        result.push_back({
            { "x", point.x },
            { "y", point.y },
            { "shape", std::clamp(point.shape, 0, 2) }
        });
    }
    return result;
}

RawPointCurveComponent SanitizePointCurveComponent(
    const nlohmann::json& value,
    const char* defaultInterpolation = "monotone-cubic-v1") {
    RawPointCurveComponent component;
    const nlohmann::json object = value.is_object() ? value : nlohmann::json::object();
    component.interpolation = defaultInterpolation;
    const auto interpolation = object.find("interpolation");
    if (interpolation != object.end() && interpolation->is_string()) {
        component.interpolation = interpolation->get<std::string>();
    }
    if (component.interpolation != "monotone-cubic-v1" &&
        component.interpolation != "legacy-segment-v1") {
        component.interpolation = defaultInterpolation;
    }
    component.points = SanitizePointCurvePoints(
        object.value("points", IdentityPointCurveJson()));
    if (object.contains("basePoints")) {
        component.basePoints = SanitizePointCurvePoints(object["basePoints"]);
    }
    return component;
}

nlohmann::json SerializePointCurveComponent(const RawPointCurveComponent& component) {
    nlohmann::json result = {
        { "interpolation", component.interpolation },
        { "points", SerializePointCurvePoints(component.points) }
    };
    if (!component.basePoints.empty()) {
        result["basePoints"] = SerializePointCurvePoints(component.basePoints);
    }
    return result;
}

float EvaluateLegacyPointCurve(
    const std::vector<RawPointCurveControlPoint>& points,
    float x) {
    if (points.empty()) {
        return std::clamp(x, 0.0f, 1.0f);
    }
    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= points.front().x) {
        return points.front().y;
    }
    for (std::size_t index = 1; index < points.size(); ++index) {
        const RawPointCurveControlPoint& a = points[index - 1u];
        const RawPointCurveControlPoint& b = points[index];
        if (x <= b.x) {
            float t = (x - a.x) / std::max(0.0001f, b.x - a.x);
            if (a.shape == 2) {
                return a.y;
            }
            if (a.shape == 0) {
                t = t * t * (3.0f - 2.0f * t);
            }
            return std::clamp(a.y + (b.y - a.y) * t, 0.0f, 1.0f);
        }
    }
    return points.back().y;
}

float PointCurveCoordinateFromScene(
    float value,
    int domain,
    float minimumEv,
    float maximumEv,
    float middleGrey) {
    if (domain == 1) {
        const float ev = std::log2(
            std::max(value, 0.000001f) / std::max(middleGrey, 0.000001f));
        return std::clamp(
            (ev - minimumEv) / std::max(0.0001f, maximumEv - minimumEv),
            0.0f,
            1.0f);
    }
    return std::clamp(value, 0.0f, 1.0f);
}

float PointCurveSceneFromCoordinate(
    float coordinate,
    int domain,
    float minimumEv,
    float maximumEv,
    float middleGrey) {
    coordinate = std::clamp(coordinate, 0.0f, 1.0f);
    if (domain == 1) {
        return std::max(middleGrey, 0.000001f) *
            std::exp2(minimumEv + coordinate * (maximumEv - minimumEv));
    }
    return coordinate;
}

} // namespace

const char* RawPointCurveChannelKey(RawPointCurveChannel channel) {
    switch (channel) {
        case RawPointCurveChannel::Composite: return "composite";
        case RawPointCurveChannel::Red: return "red";
        case RawPointCurveChannel::Green: return "green";
        case RawPointCurveChannel::Blue: return "blue";
    }
    return "composite";
}

nlohmann::json DefaultPointCurveComponentJson() {
    return {
        { "interpolation", "monotone-cubic-v1" },
        { "points", nlohmann::json::array() }
    };
}

nlohmann::json SanitizeFinishTonePointCurveJson(
    nlohmann::json finishTone,
    int storedRecipeVersion) {
    if (!finishTone.is_object()) {
        finishTone = nlohmann::json::object();
    }

    nlohmann::json curveSet =
        finishTone.contains("pointCurves") && finishTone["pointCurves"].is_object()
            ? finishTone["pointCurves"]
            : nlohmann::json::object();
    if (storedRecipeVersion < 13) {
        curveSet = nlohmann::json::object();
        for (int index = 0; index < 4; ++index) {
            curveSet[RawPointCurveChannelKey(static_cast<RawPointCurveChannel>(index))] =
                DefaultPointCurveComponentJson();
        }
        const int legacyMode = std::clamp(JsonInteger(finishTone, "mode", 1), 0, 4);
        nlohmann::json migrated = {
            { "interpolation", "legacy-segment-v1" },
            { "basePoints", finishTone.value("preparedPoints", IdentityPointCurveJson()) },
            { "points", finishTone.value("points", IdentityPointCurveJson()) }
        };
        if (legacyMode == 0) {
            finishTone["legacyLuma"] = {
                { "enabled", true },
                { "interpolation", "legacy-segment-v1" },
                { "basePoints", migrated["basePoints"] },
                { "points", migrated["points"] }
            };
        } else {
            const RawPointCurveChannel channel = static_cast<RawPointCurveChannel>(legacyMode - 1);
            curveSet[RawPointCurveChannelKey(channel)] = std::move(migrated);
        }
    }

    for (int index = 0; index < 4; ++index) {
        const char* key = RawPointCurveChannelKey(static_cast<RawPointCurveChannel>(index));
        const RawPointCurveComponent component = SanitizePointCurveComponent(
            curveSet.value(key, DefaultPointCurveComponentJson()));
        curveSet[key] = SerializePointCurveComponent(component);
    }
    finishTone["pointCurveSetVersion"] = 1;
    finishTone["pointCurves"] = std::move(curveSet);

    if (finishTone.contains("legacyLuma") && finishTone["legacyLuma"].is_object()) {
        const bool enabled = finishTone["legacyLuma"].value("enabled", false);
        RawPointCurveComponent component = SanitizePointCurveComponent(
            finishTone["legacyLuma"],
            "legacy-segment-v1");
        finishTone["legacyLuma"] = SerializePointCurveComponent(component);
        finishTone["legacyLuma"]["enabled"] = enabled;
    }
    return finishTone;
}

RawPointCurveSet PointCurveSetFromFinishToneJson(const nlohmann::json& finishTone) {
    const nlohmann::json sanitized = SanitizeFinishTonePointCurveJson(
        finishTone,
        JsonInteger(finishTone, "pointCurveSetVersion", 0) == 1 ? 13 : 12);
    RawPointCurveSet result;
    result.version = 1;
    const nlohmann::json& pointCurves = sanitized["pointCurves"];
    for (int index = 0; index < 4; ++index) {
        const char* key = RawPointCurveChannelKey(static_cast<RawPointCurveChannel>(index));
        result.curves[static_cast<std::size_t>(index)] =
            SanitizePointCurveComponent(pointCurves[key]);
    }
    if (sanitized.contains("legacyLuma") && sanitized["legacyLuma"].is_object()) {
        result.legacyLumaEnabled = sanitized["legacyLuma"].value("enabled", false);
        result.legacyLuma = SanitizePointCurveComponent(
            sanitized["legacyLuma"],
            "legacy-segment-v1");
    }
    return result;
}

void StorePointCurveSetInFinishToneJson(
    nlohmann::json& finishTone,
    const RawPointCurveSet& curveSet) {
    finishTone = SanitizeFinishTonePointCurveJson(finishTone, 13);
    finishTone["pointCurveSetVersion"] = 1;
    for (int index = 0; index < 4; ++index) {
        const RawPointCurveComponent& component =
            curveSet.curves[static_cast<std::size_t>(index)];
        finishTone["pointCurves"][RawPointCurveChannelKey(
            static_cast<RawPointCurveChannel>(index))] =
            SerializePointCurveComponent(SanitizePointCurveComponent(
                SerializePointCurveComponent(component)));
    }
    if (curveSet.legacyLumaEnabled ||
        !IsIdentityRawPointCurveComponent(curveSet.legacyLuma)) {
        finishTone["legacyLuma"] = SerializePointCurveComponent(
            SanitizePointCurveComponent(
                SerializePointCurveComponent(curveSet.legacyLuma),
                "legacy-segment-v1"));
        finishTone["legacyLuma"]["enabled"] = curveSet.legacyLumaEnabled;
    } else {
        finishTone.erase("legacyLuma");
    }
}

RawPointCurveComponent PointCurveComponentFromFinishToneJson(
    const nlohmann::json& finishTone,
    RawPointCurveChannel channel) {
    return PointCurveSetFromFinishToneJson(finishTone)
        .curves[static_cast<std::size_t>(channel)];
}

void StorePointCurveComponentInFinishToneJson(
    nlohmann::json& finishTone,
    RawPointCurveChannel channel,
    const RawPointCurveComponent& component) {
    finishTone = SanitizeFinishTonePointCurveJson(std::move(finishTone), 13);
    finishTone["pointCurves"][RawPointCurveChannelKey(channel)] =
        SerializePointCurveComponent(SanitizePointCurveComponent(
            SerializePointCurveComponent(component)));
}

float EvaluateRawPointCurve(
    const std::vector<RawPointCurveControlPoint>& points,
    const std::string& interpolation,
    float x) {
    if (interpolation != "monotone-cubic-v1" || points.size() < 3u) {
        return EvaluateLegacyPointCurve(points, x);
    }

    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= points.front().x) return points.front().y;
    if (x >= points.back().x) return points.back().y;

    const std::size_t count = std::min(points.size(), kMaxRawPointCurvePoints);
    std::array<float, kMaxRawPointCurvePoints> widths {};
    std::array<float, kMaxRawPointCurvePoints> slopes {};
    for (std::size_t index = 0; index + 1u < count; ++index) {
        widths[index] = std::max(0.0001f, points[index + 1u].x - points[index].x);
        slopes[index] = (points[index + 1u].y - points[index].y) / widths[index];
    }
    std::array<float, kMaxRawPointCurvePoints> tangents {};
    tangents.front() = slopes.front();
    tangents.back() = slopes.back();
    for (std::size_t index = 1; index + 1u < count; ++index) {
        if (slopes[index - 1u] * slopes[index] <= 0.0f) {
            tangents[index] = 0.0f;
        } else {
            const float w1 = 2.0f * widths[index] + widths[index - 1u];
            const float w2 = widths[index] + 2.0f * widths[index - 1u];
            tangents[index] = (w1 + w2) /
                (w1 / slopes[index - 1u] + w2 / slopes[index]);
        }
    }

    std::size_t segment = 0;
    while (segment + 1u < count && x > points[segment + 1u].x) {
        ++segment;
    }
    segment = std::min(segment, count - 2u);
    const float width = widths[segment];
    const float t = std::clamp((x - points[segment].x) / width, 0.0f, 1.0f);
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    const float value =
        h00 * points[segment].y +
        h10 * width * tangents[segment] +
        h01 * points[segment + 1u].y +
        h11 * width * tangents[segment + 1u];
    const float minimum = std::min(points[segment].y, points[segment + 1u].y);
    const float maximum = std::max(points[segment].y, points[segment + 1u].y);
    return std::clamp(value, minimum, maximum);
}

float EvaluateRawPointCurveComponent(const RawPointCurveComponent& component, float x) {
    float value = std::clamp(x, 0.0f, 1.0f);
    if (!component.basePoints.empty()) {
        value = EvaluateRawPointCurve(
            component.basePoints,
            component.interpolation,
            value);
    }
    return EvaluateRawPointCurve(component.points, component.interpolation, value);
}

bool IsIdentityRawPointCurveComponent(const RawPointCurveComponent& component) {
    auto identity = [](const std::vector<RawPointCurveControlPoint>& points) {
        if (points.empty()) return true;
        for (const RawPointCurveControlPoint& point : points) {
            if (std::abs(point.x - point.y) > 0.0001f) return false;
        }
        return true;
    };
    return identity(component.basePoints) && identity(component.points);
}

std::array<float, 3> EvaluateFinishTonePointCurveRgb(
    const nlohmann::json& finishTone,
    const std::array<float, 3>& sceneRgb) {
    const RawPointCurveSet curveSet = PointCurveSetFromFinishToneJson(finishTone);
    const int domain = std::clamp(finishTone.value("domain", 1), 0, 1);
    const float minimumEv = ClampFinite(
        finishTone.value("logMinEv", -10.0f), -10.0f, -20.0f, 0.0f);
    const float maximumEv = std::max(
        minimumEv + 0.1f,
        ClampFinite(finishTone.value("logMaxEv", 6.0f), 6.0f, 0.0f, 20.0f));
    const float middleGrey = ClampFinite(
        finishTone.value("middleGrey", 0.18f), 0.18f, 0.01f, 1.0f);

    std::array<float, 3> rgb {
        std::max(0.0f, sceneRgb[0]),
        std::max(0.0f, sceneRgb[1]),
        std::max(0.0f, sceneRgb[2])
    };
    if (curveSet.legacyLumaEnabled) {
        const float oldLuma =
            0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
        const float coordinate = PointCurveCoordinateFromScene(
            oldLuma, domain, minimumEv, maximumEv, middleGrey);
        const float newLuma = PointCurveSceneFromCoordinate(
            EvaluateRawPointCurveComponent(curveSet.legacyLuma, coordinate),
            domain,
            minimumEv,
            maximumEv,
            middleGrey);
        const float gain = newLuma / std::max(oldLuma, 0.000001f);
        for (float& value : rgb) value *= gain;
    }

    const RawPointCurveComponent& composite =
        curveSet.curves[static_cast<std::size_t>(RawPointCurveChannel::Composite)];
    for (int channel = 0; channel < 3; ++channel) {
        float coordinate = PointCurveCoordinateFromScene(
            rgb[static_cast<std::size_t>(channel)],
            domain,
            minimumEv,
            maximumEv,
            middleGrey);
        coordinate = EvaluateRawPointCurveComponent(composite, coordinate);
        coordinate = EvaluateRawPointCurveComponent(
            curveSet.curves[static_cast<std::size_t>(channel + 1)],
            coordinate);
        rgb[static_cast<std::size_t>(channel)] = PointCurveSceneFromCoordinate(
            coordinate, domain, minimumEv, maximumEv, middleGrey);
    }
    return rgb;
}

nlohmann::json DefaultFinishToneJson() {
    nlohmann::json pointCurves = nlohmann::json::object();
    for (int index = 0; index < 4; ++index) {
        pointCurves[RawPointCurveChannelKey(
            static_cast<RawPointCurveChannel>(index))] =
            DefaultPointCurveComponentJson();
    }
    return {
        { "type", "ToneCurve" },
        { "mode", 1 },
        { "domain", 1 },
        { "samplingBasis", 0 },
        { "targetingMode", 1 },
        { "targetAffectWidth", 0.08f },
        { "autoAnchorProtection", true },
        { "protectEndpointsDuringTargeting", true },
        { "targetShadowProtection", 0.65f },
        { "targetHighlightProtection", 0.65f },
        { "localBaselineEnabled", false },
        { "foundationAdaptiveAssist", false },
        { "foundationPreserveHue", true },
        { "preparedPoints", DefaultToneCurveLayerPointsJson() },
        { "points", DefaultToneCurveLayerPointsJson() },
        { "freeEndpoints", true },
        { "activeGraphView", 0 },
        { "pointCurveSetVersion", 1 },
        { "pointCurves", std::move(pointCurves) },
        { "logMinEv", -10.0f },
        { "logMaxEv", 6.0f },
        { "middleGrey", 0.18f }
    };
}

nlohmann::json DefaultViewTransformJson() {
    return {
        { "type", "ViewTransform" },
        { "enabled", true },
        { "exposure", 0.0f },
        { "blackEv", -8.0f },
        { "whiteEv", 4.0f },
        { "middleGrey", 0.18f },
        { "shoulder", 0.45f },
        { "toe", 0.18f },
        { "contrast", 1.0f },
        { "saturation", 1.0f },
        { "preserveHue", true },
        { "debugFalseColor", false },
        { "inputWorkingSpace", "linear-rec2020-d65" },
        { "encodeSrgbOutput", true }
    };
}

float EvaluateViewTransformDisplayLuma(
    float input,
    float exposure,
    float blackEv,
    float whiteEv,
    float middleGrey,
    float shoulder,
    float toe,
    float contrast) {
    // Keep this scalar reference in lockstep with filmicCurve() in
    // ToneLayerRendering.cpp. RAW Lab uses it only to visualize the existing
    // display transform; it does not define or replace the render path.
    const float black = middleGrey * std::exp2(blackEv);
    const float white = middleGrey * std::exp2(whiteEv);
    float x = std::max(0.0f, input * std::exp2(exposure) - black);
    float normalized = x / std::max(0.000001f, white - black);
    normalized = std::pow(std::max(0.0f, normalized), std::max(0.05f, contrast));
    const float clampedToe = std::clamp(toe, 0.0f, 1.0f);
    const float toeMapped =
        (normalized + clampedToe * normalized / (normalized + 0.18f)) /
        (1.0f + clampedToe);
    normalized = normalized * (1.0f - clampedToe) + toeMapped * clampedToe;
    const float safeShoulder = std::max(0.001f, shoulder);
    const float mapped = normalized / (normalized + safeShoulder);
    const float whiteMapped = 1.0f / (1.0f + safeShoulder);
    return std::clamp(mapped / std::max(0.0001f, whiteMapped), 0.0f, 1.0f);
}

nlohmann::json FinishToneJsonFromLegacyToneCurve(const RawToneCurveRecipe& toneCurve) {
    nlohmann::json finishTone = DefaultFinishToneJson();
    if (IsIdentityToneCurve(toneCurve)) {
        return finishTone;
    }

    nlohmann::json points = nlohmann::json::array();
    for (const Raw::RawToneCurvePoint& point : BuildRawToneCurveSettingsPoints(toneCurve)) {
        points.push_back({
            { "x", point.input },
            { "y", point.output },
            { "shape", 1 }
        });
    }
    if (points.size() >= 2) {
        finishTone["domain"] = 0;
        finishTone["points"] = points;
        finishTone["preparedPoints"] = std::move(points);
    }
    return SanitizeFinishTonePointCurveJson(std::move(finishTone), 12);
}

std::vector<RawLocalRangePoint> DefaultLocalRangePoints(float minEv, float maxEv) {
    minEv = ClampFinite(minEv, -8.0f, -16.0f, 0.0f);
    maxEv = ClampFinite(maxEv, 6.0f, 0.0f, 16.0f);
    if (maxEv <= minEv + 0.1f) {
        maxEv = minEv + 0.1f;
    }

    const float middleEv = std::clamp(0.0f, minEv, maxEv);
    return {
        RawLocalRangePoint{ minEv, 0.0f },
        RawLocalRangePoint{ middleEv, 0.0f },
        RawLocalRangePoint{ maxEv, 0.0f }
    };
}

RawLocalRangeRecipe DefaultLocalRangeRecipe() {
    RawLocalRangeRecipe localRange;
    localRange.points = DefaultLocalRangePoints(localRange.minEv, localRange.maxEv);
    return localRange;
}

RawLocalRangeRecipe SanitizeLocalRangeRecipe(RawLocalRangeRecipe localRange) {
    RawLocalRangeRecipe defaults = DefaultLocalRangeRecipe();
    localRange.strength = ClampFinite(localRange.strength, defaults.strength, 0.0f, 1.0f);
    localRange.middleGrey = ClampFinite(localRange.middleGrey, defaults.middleGrey, 0.01f, 1.0f);
    localRange.minEv = ClampFinite(localRange.minEv, defaults.minEv, -16.0f, 0.0f);
    localRange.maxEv = ClampFinite(localRange.maxEv, defaults.maxEv, 0.0f, 16.0f);
    if (localRange.maxEv <= localRange.minEv + 0.1f) {
        localRange.maxEv = localRange.minEv + 0.1f;
    }
    localRange.smoothness = ClampFinite(localRange.smoothness, defaults.smoothness, 0.0f, 1.0f);
    localRange.edgeProtection = ClampFinite(localRange.edgeProtection, defaults.edgeProtection, 0.0f, 1.0f);
    localRange.detailProtection = ClampFinite(localRange.detailProtection, defaults.detailProtection, 0.0f, 1.0f);
    localRange.highlightProtection = ClampFinite(localRange.highlightProtection, defaults.highlightProtection, 0.0f, 1.0f);
    if (!IsSupportedLocalRangeMaskPreviewMode(localRange.maskPreviewMode)) {
        localRange.maskPreviewMode = defaults.maskPreviewMode;
    }
    if (!IsSupportedLocalRangeRegionMaskMode(localRange.regionMaskMode)) {
        localRange.regionMaskMode = defaults.regionMaskMode;
    }
    localRange.regionMaskCenterX = ClampFinite(localRange.regionMaskCenterX, defaults.regionMaskCenterX, 0.0f, 1.0f);
    localRange.regionMaskCenterY = ClampFinite(localRange.regionMaskCenterY, defaults.regionMaskCenterY, 0.0f, 1.0f);
    localRange.regionMaskAngleDegrees = ClampFinite(
        localRange.regionMaskAngleDegrees,
        defaults.regionMaskAngleDegrees,
        -180.0f,
        180.0f);
    localRange.regionMaskSize = ClampFinite(localRange.regionMaskSize, defaults.regionMaskSize, 0.02f, 1.5f);
    localRange.regionMaskFeather = ClampFinite(localRange.regionMaskFeather, defaults.regionMaskFeather, 0.0f, 1.0f);
    localRange.regionMaskLowEv = ClampFinite(localRange.regionMaskLowEv, defaults.regionMaskLowEv, -16.0f, 16.0f);
    localRange.regionMaskHighEv = ClampFinite(localRange.regionMaskHighEv, defaults.regionMaskHighEv, -16.0f, 16.0f);
    if (localRange.regionMaskHighEv <= localRange.regionMaskLowEv + 0.1f) {
        localRange.regionMaskHighEv = std::min(16.0f, localRange.regionMaskLowEv + 0.1f);
        if (localRange.regionMaskHighEv <= localRange.regionMaskLowEv + 0.001f) {
            localRange.regionMaskLowEv = std::max(-16.0f, localRange.regionMaskHighEv - 0.1f);
        }
    }
    localRange.colorMaskTargetR = ClampFinite(localRange.colorMaskTargetR, defaults.colorMaskTargetR, 0.0f, 32.0f);
    localRange.colorMaskTargetG = ClampFinite(localRange.colorMaskTargetG, defaults.colorMaskTargetG, 0.0f, 32.0f);
    localRange.colorMaskTargetB = ClampFinite(localRange.colorMaskTargetB, defaults.colorMaskTargetB, 0.0f, 32.0f);
    localRange.colorMaskHueWidth = ClampFinite(localRange.colorMaskHueWidth, defaults.colorMaskHueWidth, 0.02f, 1.20f);
    localRange.colorMaskFeather = ClampFinite(localRange.colorMaskFeather, defaults.colorMaskFeather, 0.0f, 1.0f);
    localRange.colorMaskMinChroma = ClampFinite(localRange.colorMaskMinChroma, defaults.colorMaskMinChroma, 0.0f, 1.0f);

    std::vector<RawLocalRangeTargetZone> targetZones;
    targetZones.reserve(std::min(localRange.targetZones.size(), kMaxRawLocalRangeTargetZones));
    std::unordered_set<std::string> targetZoneIds;
    for (std::size_t sourceIndex = 0;
         sourceIndex < localRange.targetZones.size() &&
             targetZones.size() < kMaxRawLocalRangeTargetZones;
         ++sourceIndex) {
        RawLocalRangeTargetZone zone = localRange.targetZones[sourceIndex];
        zone.centerEv = ClampFinite(zone.centerEv, 0.0f, localRange.minEv, localRange.maxEv);
        zone.coreHalfWidthEv = ClampFinite(zone.coreHalfWidthEv, 0.35f, 0.05f, 4.0f);
        zone.featherEv = ClampFinite(zone.featherEv, 0.65f, 0.02f, 4.0f);
        zone.deltaEv = ClampFinite(zone.deltaEv, 0.0f, -4.0f, 4.0f);
        zone.targetUPrime = ClampFinite(zone.targetUPrime, 0.19783f, 0.0f, 0.70f);
        zone.targetVPrime = ClampFinite(zone.targetVPrime, 0.46832f, 0.0f, 0.70f);
        zone.targetChroma = ClampFinite(zone.targetChroma, 0.0f, 0.0f, 0.70f);
        zone.colorRadius = ClampFinite(zone.colorRadius, 0.025f, 0.002f, 0.25f);
        zone.colorFeather = ClampFinite(zone.colorFeather, 0.035f, 0.002f, 0.25f);
        if (zone.name.size() > 64) {
            zone.name.resize(64);
        }
        if (zone.id.empty() || targetZoneIds.count(zone.id) != 0) {
            const std::string baseId = "zone-" + std::to_string(sourceIndex + 1);
            zone.id = baseId;
            int suffix = 2;
            while (targetZoneIds.count(zone.id) != 0) {
                zone.id = baseId + "-" + std::to_string(suffix++);
            }
        }
        targetZoneIds.insert(zone.id);

        std::vector<RawLocalRangeTargetSeed> seeds;
        seeds.reserve(std::min(zone.seeds.size(), kMaxRawLocalRangeTargetSeeds));
        for (const RawLocalRangeTargetSeed& seed : zone.seeds) {
            if (seeds.size() >= kMaxRawLocalRangeTargetSeeds ||
                !std::isfinite(seed.sourceU) ||
                !std::isfinite(seed.sourceV)) {
                continue;
            }
            seeds.push_back({
                std::clamp(seed.sourceU, 0.0f, 1.0f),
                std::clamp(seed.sourceV, 0.0f, 1.0f)
            });
        }
        zone.seeds = std::move(seeds);
        targetZones.push_back(std::move(zone));
    }
    localRange.targetZones = std::move(targetZones);

    std::vector<RawLocalRangePoint> points;
    points.reserve(localRange.points.size());
    for (const RawLocalRangePoint& point : localRange.points) {
        if (!std::isfinite(point.ev) || !std::isfinite(point.deltaEv)) {
            continue;
        }
        points.push_back({
            std::clamp(point.ev, localRange.minEv, localRange.maxEv),
            std::clamp(point.deltaEv, -4.0f, 4.0f)
        });
    }
    if (points.empty()) {
        points = DefaultLocalRangePoints(localRange.minEv, localRange.maxEv);
    }

    std::sort(points.begin(), points.end(), [](const RawLocalRangePoint& a, const RawLocalRangePoint& b) {
        return a.ev < b.ev;
    });

    std::vector<RawLocalRangePoint> uniquePoints;
    uniquePoints.reserve(points.size());
    for (const RawLocalRangePoint& point : points) {
        if (!uniquePoints.empty() && std::abs(uniquePoints.back().ev - point.ev) < 0.001f) {
            uniquePoints.back() = point;
            continue;
        }
        uniquePoints.push_back(point);
    }
    while (uniquePoints.size() > kMaxRawLocalRangePoints) {
        uniquePoints.erase(uniquePoints.end() - 1);
    }
    localRange.points = std::move(uniquePoints);
    return localRange;
}

RawRgbDenoiseRecipe SanitizeRgbDenoiseRecipe(RawRgbDenoiseRecipe rgbDenoise) {
    const RawRgbDenoiseRecipe defaults;
    if (rgbDenoise.method != RawRgbDenoiseMethod::ClassicalMultiscaleV1 &&
        rgbDenoise.method != RawRgbDenoiseMethod::RestormerRealV1 &&
        rgbDenoise.method != RawRgbDenoiseMethod::RestormerGaussianBlindV1) {
        rgbDenoise.method = defaults.method;
    }
    if (rgbDenoise.mapping != RawRgbDenoiseMapping::SceneLinearSafeV1 &&
        rgbDenoise.mapping != RawRgbDenoiseMapping::ProcessedRgbMatchV1) {
        rgbDenoise.mapping = defaults.mapping;
    }
    if (rgbDenoise.packageId.empty()) {
        rgbDenoise.packageId = kRestormerDenoisePackageId;
    }
    if (rgbDenoise.adapterVersion.empty()) {
        rgbDenoise.adapterVersion = kRestormerDenoiseAdapterVersion;
    }
    rgbDenoise.colorNoise =
        ClampFinite(rgbDenoise.colorNoise, defaults.colorNoise, 0.0f, 1.0f);
    rgbDenoise.luminanceNoise =
        ClampFinite(rgbDenoise.luminanceNoise, defaults.luminanceNoise, 0.0f, 1.0f);
    rgbDenoise.detailProtection =
        ClampFinite(rgbDenoise.detailProtection, defaults.detailProtection, 0.0f, 1.0f);
    return rgbDenoise;
}

RawLocalRangeRecipe ApplyLocalRangePreset(RawLocalRangeRecipe localRange, RawLocalRangePreset preset) {
    if (preset == RawLocalRangePreset::Reset) {
        const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(std::move(localRange));
        RawLocalRangeRecipe reset = DefaultLocalRangeRecipe();
        reset.targetZoneCombineMode = sanitized.targetZoneCombineMode;
        reset.targetZones = sanitized.targetZones;
        reset.enabled = std::any_of(
            reset.targetZones.begin(),
            reset.targetZones.end(),
            [](const RawLocalRangeTargetZone& zone) {
                return zone.enabled && std::abs(zone.deltaEv) > 0.0001f;
            });
        return reset;
    }

    localRange = SanitizeLocalRangeRecipe(localRange);
    localRange.enabled = true;
    localRange.strength = 1.0f;
    localRange.smoothness = std::max(localRange.smoothness, 0.72f);
    localRange.edgeProtection = std::max(localRange.edgeProtection, 0.78f);
    localRange.detailProtection = std::max(localRange.detailProtection, 0.80f);
    localRange.highlightProtection = std::max(localRange.highlightProtection, 0.55f);

    switch (preset) {
        case RawLocalRangePreset::OpenShadows:
            localRange.points = {
                { localRange.minEv, 0.0f },
                { -5.0f, 1.15f },
                { -2.0f, 0.45f },
                { 0.0f, 0.0f },
                { localRange.maxEv, 0.0f }
            };
            break;
        case RawLocalRangePreset::HoldHighlights:
            localRange.points = {
                { localRange.minEv, 0.0f },
                { 0.0f, 0.0f },
                { 2.0f, -0.35f },
                { 4.0f, -0.95f },
                { localRange.maxEv, -0.75f }
            };
            break;
        case RawLocalRangePreset::CompressRange:
            localRange.points = {
                { localRange.minEv, 0.95f },
                { -3.0f, 0.60f },
                { 0.0f, 0.0f },
                { 3.0f, -0.60f },
                { localRange.maxEv, -0.90f }
            };
            break;
        case RawLocalRangePreset::Reset:
        default:
            break;
    }

    return SanitizeLocalRangeRecipe(localRange);
}

RawLocalRangeRecipe LocalRangeRecipeFromLocalExposure(
    const RawLocalExposureRecipe& localExposureInput,
    const RawLocalRangeRecipe& baseLocalRange) {
    const RawLocalExposureRecipe localExposure = SanitizeLocalExposureRecipe(localExposureInput);
    RawLocalRangeRecipe localRange = SanitizeLocalRangeRecipe(baseLocalRange);
    const bool hasEffect = localExposure.enabled &&
        localExposure.amount > 0.0001f &&
        (localExposure.shadowLiftEv > 0.0001f ||
            -localExposure.highlightCompressionEv > 0.0001f ||
            std::abs(localExposure.localBaselineEv) > 0.0001f);
    if (!hasEffect) {
        return localRange;
    }

    const float amount = std::clamp(localExposure.amount, 0.0f, 1.0f);
    const float baseline = std::clamp(localExposure.localBaselineEv, -1.25f, 1.25f);
    const float shadowDelta = std::clamp(localExposure.shadowLiftEv + baseline * 0.35f, -4.0f, 4.0f);
    const float midShadowDelta = std::clamp(localExposure.shadowLiftEv * 0.45f + baseline * 0.65f, -4.0f, 4.0f);
    const float midDelta = std::clamp(baseline, -4.0f, 4.0f);
    const float midHighlightDelta =
        std::clamp(localExposure.highlightCompressionEv * 0.45f + baseline * 0.65f, -4.0f, 4.0f);
    const float highlightDelta = std::clamp(localExposure.highlightCompressionEv + baseline * 0.35f, -4.0f, 4.0f);

    localRange.enabled = true;
    localRange.strength = amount;
    localRange.smoothness = std::max(localRange.smoothness, localExposure.smoothGradientProtection);
    localRange.edgeProtection = std::max(localRange.edgeProtection, localExposure.haloGuard);
    localRange.detailProtection = std::max(
        localRange.detailProtection,
        std::clamp(
            (localExposure.noiseGuardBias + localExposure.highlightGuardBias + localExposure.shadowGuardBias) / 6.0f + 0.5f,
            0.0f,
            1.0f));
    localRange.highlightProtection = std::max(
        localRange.highlightProtection,
        std::clamp(localExposure.highlightGuardBias * 0.5f + 0.5f, 0.0f, 1.0f));
    localRange.points = {
        { localRange.minEv, shadowDelta },
        { -4.0f, midShadowDelta },
        { 0.0f, midDelta },
        { 3.0f, midHighlightDelta },
        { localRange.maxEv, highlightDelta }
    };
    return SanitizeLocalRangeRecipe(localRange);
}

namespace {

float EvaluateSanitizedLocalRangeControlDeltaEv(
    const RawLocalRangeRecipe& sanitized,
    float sceneEv) {
    if (sanitized.points.size() < 2 || !std::isfinite(sceneEv)) {
        return 0.0f;
    }

    const float clampedEv = std::clamp(sceneEv, sanitized.minEv, sanitized.maxEv);
    RawLocalRangePoint previous = sanitized.points.front();
    if (clampedEv <= previous.ev) {
        return previous.deltaEv;
    }

    for (std::size_t i = 1; i < sanitized.points.size(); ++i) {
        const RawLocalRangePoint current = sanitized.points[i];
        if (clampedEv <= current.ev) {
            const float span = std::max(current.ev - previous.ev, 0.0001f);
            const float t = std::clamp((clampedEv - previous.ev) / span, 0.0f, 1.0f);
            return previous.deltaEv + (current.deltaEv - previous.deltaEv) * t;
        }
        previous = current;
    }

    return previous.deltaEv;
}

} // namespace

float EvaluateLocalRangeControlDeltaEv(const RawLocalRangeRecipe& localRange, float sceneEv) {
    return EvaluateSanitizedLocalRangeControlDeltaEv(
        SanitizeLocalRangeRecipe(localRange),
        sceneEv);
}

float EvaluateLocalRangeDeltaEv(const RawLocalRangeRecipe& localRange, float sceneEv) {
    const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(localRange);
    if (!sanitized.enabled || sanitized.strength <= 0.0001f) {
        return 0.0f;
    }
    return sanitized.strength *
        EvaluateSanitizedLocalRangeControlDeltaEv(sanitized, sceneEv);
}

float LocalRangeExposureScaleForLuma(const RawLocalRangeRecipe& localRange, float sceneLuma) {
    const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(localRange);
    if (!sanitized.enabled || sanitized.strength <= 0.0001f || !std::isfinite(sceneLuma)) {
        return 1.0f;
    }

    const float safeLuma = std::max(sceneLuma, 0.000001f);
    const float sceneEv = std::log2(safeLuma / sanitized.middleGrey);
    const float deltaEv = EvaluateLocalRangeDeltaEv(sanitized, sceneEv);
    if (std::abs(deltaEv) <= 0.0001f) {
        return 1.0f;
    }
    return std::exp2(deltaEv);
}

float EvaluateLocalRangeRegionMask(
    const RawLocalRangeRecipe& localRange,
    float normalizedX,
    float normalizedY,
    float sceneEv) {
    const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(localRange);
    if (!sanitized.regionMaskEnabled ||
        !std::isfinite(normalizedX) ||
        !std::isfinite(normalizedY) ||
        !std::isfinite(sceneEv)) {
        return 1.0f;
    }

    const float x = std::clamp(normalizedX, 0.0f, 1.0f);
    const float y = std::clamp(normalizedY, 0.0f, 1.0f);
    float mask = 1.0f;
    if (sanitized.regionMaskMode == "linear-gradient") {
        constexpr float kPi = 3.14159265358979323846f;
        const float radians = sanitized.regionMaskAngleDegrees * kPi / 180.0f;
        const float dx = std::cos(radians);
        const float dy = std::sin(radians);
        const float projection =
            (x - sanitized.regionMaskCenterX) * dx +
            (y - sanitized.regionMaskCenterY) * dy;
        const float softWidth = std::max(
            0.001f,
            sanitized.regionMaskSize * (0.08f + 0.92f * sanitized.regionMaskFeather));
        mask = SmoothStep(-softWidth, softWidth, projection);
    } else if (sanitized.regionMaskMode == "radial-gradient") {
        const float dx = x - sanitized.regionMaskCenterX;
        const float dy = y - sanitized.regionMaskCenterY;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const float feather = sanitized.regionMaskSize * sanitized.regionMaskFeather;
        const float inner = std::max(0.0f, sanitized.regionMaskSize - feather);
        const float outer = sanitized.regionMaskSize + feather;
        mask = 1.0f - SmoothStep(inner, outer, distance);
    } else if (sanitized.regionMaskMode == "luminance-range") {
        const float featherEv = std::max(0.02f, sanitized.regionMaskFeather * 4.0f);
        const float lowMask = SmoothStep(
            sanitized.regionMaskLowEv - featherEv,
            sanitized.regionMaskLowEv,
            sceneEv);
        const float highMask = 1.0f - SmoothStep(
            sanitized.regionMaskHighEv,
            sanitized.regionMaskHighEv + featherEv,
            sceneEv);
        mask = lowMask * highMask;
    }

    mask = std::clamp(mask, 0.0f, 1.0f);
    return sanitized.regionMaskInvert ? 1.0f - mask : mask;
}

float EvaluateLocalRangeColorMask(
    const RawLocalRangeRecipe& localRange,
    float sceneR,
    float sceneG,
    float sceneB) {
    const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(localRange);
    if (!sanitized.colorMaskEnabled ||
        !std::isfinite(sceneR) ||
        !std::isfinite(sceneG) ||
        !std::isfinite(sceneB)) {
        return 1.0f;
    }

    const std::array<float, 3> targetDirection = ColorDirection(
        sanitized.colorMaskTargetR,
        sanitized.colorMaskTargetG,
        sanitized.colorMaskTargetB);
    const std::array<float, 3> sampleDirection = ColorDirection(sceneR, sceneG, sceneB);
    const float dr = targetDirection[0] - sampleDirection[0];
    const float dg = targetDirection[1] - sampleDirection[1];
    const float db = targetDirection[2] - sampleDirection[2];
    const float directionDistance = std::sqrt(dr * dr + dg * dg + db * db);
    const float feather = std::max(0.015f, sanitized.colorMaskFeather * 0.65f);
    const float hueMask = 1.0f - SmoothStep(
        sanitized.colorMaskHueWidth,
        sanitized.colorMaskHueWidth + feather,
        directionDistance);

    const float targetChroma = ColorChroma(
        sanitized.colorMaskTargetR,
        sanitized.colorMaskTargetG,
        sanitized.colorMaskTargetB);
    const float sampleChroma = ColorChroma(sceneR, sceneG, sceneB);
    float chromaMask = 1.0f;
    if (targetChroma >= 0.08f) {
        chromaMask = SmoothStep(
            sanitized.colorMaskMinChroma,
            std::min(1.0f, sanitized.colorMaskMinChroma + 0.12f),
            sampleChroma);
    } else {
        const float neutralFeather = std::max(0.04f, sanitized.colorMaskFeather * 0.25f);
        chromaMask = 1.0f - SmoothStep(
            sanitized.colorMaskMinChroma,
            std::min(1.0f, sanitized.colorMaskMinChroma + neutralFeather),
            sampleChroma);
    }

    return std::clamp(hueMask * chromaMask, 0.0f, 1.0f);
}

std::array<float, 3> SceneLinearRgbToUvChroma(
    float sceneR,
    float sceneG,
    float sceneB,
    Raw::RawWorkingSpace workingSpace) {
    const float r = std::max(std::isfinite(sceneR) ? sceneR : 0.0f, 0.0f);
    const float g = std::max(std::isfinite(sceneG) ? sceneG : 0.0f, 0.0f);
    const float b = std::max(std::isfinite(sceneB) ? sceneB : 0.0f, 0.0f);

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (workingSpace == Raw::RawWorkingSpace::LinearRec2020D65) {
        x = 0.63695805f * r + 0.14461690f * g + 0.16888098f * b;
        y = 0.26270021f * r + 0.67799807f * g + 0.05930172f * b;
        z = 0.00000000f * r + 0.02807269f * g + 1.06098506f * b;
    } else {
        x = 0.41245640f * r + 0.35757610f * g + 0.18043750f * b;
        y = 0.21267290f * r + 0.71515220f * g + 0.07217500f * b;
        z = 0.01933390f * r + 0.11919200f * g + 0.95030410f * b;
    }

    constexpr float kD65UPrime = 0.19783001f;
    constexpr float kD65VPrime = 0.46831999f;
    const float denominator = x + 15.0f * y + 3.0f * z;
    if (denominator <= 0.0000001f) {
        return { kD65UPrime, kD65VPrime, 0.0f };
    }
    const float uPrime = 4.0f * x / denominator;
    const float vPrime = 9.0f * y / denominator;
    const float du = uPrime - kD65UPrime;
    const float dv = vPrime - kD65VPrime;
    return {
        std::clamp(uPrime, 0.0f, 0.70f),
        std::clamp(vPrime, 0.0f, 0.70f),
        std::clamp(std::sqrt(du * du + dv * dv), 0.0f, 0.70f)
    };
}

float EvaluateLocalRangeTargetZoneTonalWeight(
    const RawLocalRangeTargetZone& zoneInput,
    float sceneEv) {
    if (!zoneInput.enabled || !std::isfinite(sceneEv)) {
        return 0.0f;
    }
    const float centerEv = std::isfinite(zoneInput.centerEv) ? zoneInput.centerEv : 0.0f;
    const float coreHalfWidth = std::clamp(
        std::isfinite(zoneInput.coreHalfWidthEv) ? zoneInput.coreHalfWidthEv : 0.35f,
        0.05f,
        4.0f);
    const float feather = std::clamp(
        std::isfinite(zoneInput.featherEv) ? zoneInput.featherEv : 0.65f,
        0.02f,
        4.0f);
    const float distance = std::abs(sceneEv - centerEv);
    return 1.0f - SmoothStep(coreHalfWidth, coreHalfWidth + feather, distance);
}

float EvaluateLocalRangeTargetZoneColorWeight(
    const RawLocalRangeTargetZone& zoneInput,
    float sceneR,
    float sceneG,
    float sceneB,
    Raw::RawWorkingSpace workingSpace) {
    if (!zoneInput.enabled) {
        return 0.0f;
    }
    if (!zoneInput.colorEnabled) {
        return 1.0f;
    }
    const std::array<float, 3> sample =
        SceneLinearRgbToUvChroma(sceneR, sceneG, sceneB, workingSpace);
    const float targetU = std::clamp(zoneInput.targetUPrime, 0.0f, 0.70f);
    const float targetV = std::clamp(zoneInput.targetVPrime, 0.0f, 0.70f);
    const float radius = std::clamp(zoneInput.colorRadius, 0.002f, 0.25f);
    const float feather = std::clamp(zoneInput.colorFeather, 0.002f, 0.25f);
    const float du = sample[0] - targetU;
    const float dv = sample[1] - targetV;
    const float distance = std::sqrt(du * du + dv * dv);
    float weight = 1.0f - SmoothStep(radius, radius + feather, distance);

    const float targetChroma = std::clamp(zoneInput.targetChroma, 0.0f, 0.70f);
    if (targetChroma < 0.018f) {
        weight *= 1.0f - SmoothStep(
            std::max(0.018f, radius),
            std::max(0.018f, radius) + feather,
            sample[2]);
    } else {
        weight *= SmoothStep(0.006f, 0.020f, sample[2]);
    }
    return std::clamp(weight, 0.0f, 1.0f);
}

float EvaluateLocalRangeTargetZoneDeltaEv(
    const RawLocalRangeTargetZone& zone,
    float sceneEv,
    float sceneR,
    float sceneG,
    float sceneB,
    Raw::RawWorkingSpace workingSpace,
    float selectedAreaWeight) {
    const float tonalWeight = EvaluateLocalRangeTargetZoneTonalWeight(zone, sceneEv);
    const float colorWeight = EvaluateLocalRangeTargetZoneColorWeight(
        zone,
        sceneR,
        sceneG,
        sceneB,
        workingSpace);
    return std::clamp(zone.deltaEv, -4.0f, 4.0f) *
        tonalWeight *
        colorWeight *
        std::clamp(selectedAreaWeight, 0.0f, 1.0f);
}

float CombineLocalRangeTargetZoneDeltaEv(
    RawLocalRangeZoneCombineMode mode,
    const std::vector<float>& weightedDeltas,
    const std::vector<float>& weights) {
    const std::size_t count = std::min(weightedDeltas.size(), weights.size());
    if (count == 0) {
        return 0.0f;
    }
    if (mode == RawLocalRangeZoneCombineMode::Strongest) {
        float strongest = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            if (std::abs(weightedDeltas[i]) > std::abs(strongest)) {
                strongest = weightedDeltas[i];
            }
        }
        return std::clamp(strongest, -4.0f, 4.0f);
    }

    float sum = 0.0f;
    float weightSum = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(weightedDeltas[i]) || !std::isfinite(weights[i])) {
            continue;
        }
        sum += weightedDeltas[i];
        weightSum += std::clamp(weights[i], 0.0f, 1.0f);
    }
    if (mode == RawLocalRangeZoneCombineMode::Blend) {
        sum /= std::max(1.0f, weightSum);
    }
    return std::clamp(sum, -4.0f, 4.0f);
}

float EdgeAwareLocalRangeDeltaEvForSamples(
    const RawLocalRangeRecipe& localRange,
    float centerSceneEv,
    const std::vector<float>& sampleSceneEvs) {
    const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(localRange);
    if (!sanitized.enabled ||
        sanitized.strength <= 0.0001f ||
        sanitized.points.size() < 2 ||
        !std::isfinite(centerSceneEv)) {
        return 0.0f;
    }

    float weightedSum = centerSceneEv;
    float totalWeight = 1.0f;
    for (const float sampleSceneEv : sampleSceneEvs) {
        if (!std::isfinite(sampleSceneEv)) {
            continue;
        }
        const float weight = LocalRangeEdgeAwareWeight(sampleSceneEv - centerSceneEv, sanitized);
        weightedSum += sampleSceneEv * weight;
        totalWeight += weight;
    }

    const float smoothedSceneEv = weightedSum / std::max(totalWeight, 0.0001f);
    const float mapSceneEv =
        centerSceneEv + (smoothedSceneEv - centerSceneEv) * sanitized.smoothness;
    float deltaEv = EvaluateLocalRangeDeltaEv(sanitized, mapSceneEv);
    if (deltaEv > 0.0f) {
        const float highlightZone = SmoothStep(1.5f, std::max(sanitized.maxEv, 1.5001f), mapSceneEv);
        deltaEv *= 1.0f - sanitized.highlightProtection * highlightZone * 0.85f;
    }
    return deltaEv;
}

bool FinishStateEquals(const RawDevelopmentRecipe& a, const RawDevelopmentRecipe& b) {
    return a.finishTone.layerJson == b.finishTone.layerJson &&
        a.viewTransform.layerJson == b.viewTransform.layerJson;
}

std::size_t FinishStateHash(const RawDevelopmentRecipe& recipe) {
    std::size_t seed = std::hash<std::string>{}(recipe.finishTone.layerJson.dump());
    seed ^= std::hash<std::string>{}(recipe.viewTransform.layerJson.dump()) +
        0x9e3779b97f4a7c15ull +
        (seed << 6) +
        (seed >> 2);
    return seed;
}

bool LocalRangeStateEquals(const RawDevelopmentRecipe& a, const RawDevelopmentRecipe& b) {
    return LocalRangeJson(a.localRange) == LocalRangeJson(b.localRange);
}

std::size_t LocalRangeStateHash(const RawDevelopmentRecipe& recipe) {
    return std::hash<std::string>{}(LocalRangeJson(recipe.localRange).dump());
}

Raw::RawDevelopSettings ToRawDevelopSettings(const RawDevelopmentRecipe& recipe) {
    Raw::RawDevelopSettings settings;
    settings.processingVersion = recipe.technical.processingVersion;
    settings.demosaicMethod = recipe.technical.demosaicMethod;
    settings.workingSpace = recipe.technical.workingSpace;
    settings.applyBaselineExposure = recipe.technical.applyBaselineExposure;
    settings.encodeSrgbOutput = recipe.technical.encodeSrgbOutput;
    settings.mosaicDenoise =
        SanitizeMosaicDenoiseSettings(recipe.technical.mosaicDenoise);
    if (recipe.technical.processingVersion == Raw::RawProcessingVersion::TruthfulV1) {
        settings.highlightMode = Raw::HighlightReconstructionMode::Off;
        settings.falseColorSuppression = 0.0f;
        settings.defringeStrength = 0.0f;
        settings.highlightEdgeCleanup = 0.0f;
    }
    settings.exposureStops = recipe.preToneExposureEv;
    switch (recipe.whiteBalance.mode) {
        case WhiteBalanceMode::Auto:
            settings.whiteBalanceMode = Raw::WhiteBalanceMode::Auto;
            break;
        case WhiteBalanceMode::CustomMultipliers:
        case WhiteBalanceMode::SampledGrayPoint:
            settings.whiteBalanceMode = recipe.whiteBalance.hasMultipliers
                ? Raw::WhiteBalanceMode::Manual
                : Raw::WhiteBalanceMode::AsShot;
            break;
        case WhiteBalanceMode::AsShot:
        default:
            settings.whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
            break;
    }
    if (recipe.whiteBalance.hasMultipliers) {
        settings.manualWhiteBalance = recipe.whiteBalance.multipliers;
    }
    settings.rotationDegrees = recipe.cropRotation.rotationDegrees;
    settings.flipHorizontally = recipe.cropRotation.flipHorizontally;
    settings.flipVertically = recipe.cropRotation.flipVertically;
    return settings;
}

Raw::RawDetailFusionSettings ToRawDetailFusionSettings(const RawDevelopmentRecipe& recipe) {
    const RawLocalExposureRecipe localExposure = SanitizeLocalExposureRecipe(recipe.localExposure);
    Raw::RawDetailFusionSettings settings;
    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
    settings.autoSafetyEnabled = true;
    settings.overrideMinEv = true;
    settings.overrideMaxEv = true;
    settings.overrideBaseEv = true;
    settings.overrideNoiseProtection = false;
    settings.overrideHighlightProtection = false;
    settings.overrideShadowLiftLimit = false;
    settings.overrideWellExposedTarget = false;
    settings.strength = localExposure.amount;
    settings.maxEv = localExposure.shadowLiftEv;
    settings.minEv = localExposure.highlightCompressionEv;
    settings.baseEv = localExposure.localBaselineEv;
    settings.noiseProtectionBias = localExposure.noiseGuardBias;
    settings.highlightProtectionBias = localExposure.highlightGuardBias;
    settings.shadowLiftLimitBias = localExposure.shadowGuardBias;
    settings.smoothGradientProtection = localExposure.smoothGradientProtection;
    settings.haloGuard = localExposure.haloGuard;
    settings.invertMask = false;
    settings.maskBlackPoint = 0.0f;
    settings.maskWhitePoint = 1.0f;
    settings.maskGamma = 1.0f;
    settings.manualBlend = 0.0f;
    return settings;
}

bool IsLocalExposureEnabled(const RawDevelopmentRecipe& recipe) {
    const RawLocalExposureRecipe localExposure = SanitizeLocalExposureRecipe(recipe.localExposure);
    return localExposure.enabled &&
        localExposure.amount > 0.0001f &&
        (localExposure.shadowLiftEv > 0.0001f ||
            -localExposure.highlightCompressionEv > 0.0001f ||
            std::abs(localExposure.localBaselineEv) > 0.0001f);
}

bool IsLocalRangeEnabled(const RawLocalRangeRecipe& localRangeInput) {
    const RawLocalRangeRecipe localRange = SanitizeLocalRangeRecipe(localRangeInput);
    if (!localRange.enabled || localRange.strength <= 0.0001f) {
        return false;
    }
    for (const RawLocalRangePoint& point : localRange.points) {
        if (std::abs(point.deltaEv) > 0.0001f) {
            return true;
        }
    }
    for (const RawLocalRangeTargetZone& zone : localRange.targetZones) {
        if (zone.enabled && std::abs(zone.deltaEv) > 0.0001f) {
            return true;
        }
    }
    return false;
}

bool IsLocalRangeEnabled(const RawDevelopmentRecipe& recipe) {
    return IsLocalRangeEnabled(recipe.localRange);
}

bool IsViewTransformEnabled(const RawDevelopmentRecipe& recipe) {
    const nlohmann::json& viewTransform = recipe.viewTransform.layerJson;
    return !viewTransform.is_object() ||
        !viewTransform.contains("enabled") ||
        !viewTransform["enabled"].is_boolean() ||
        viewTransform["enabled"].get<bool>();
}

nlohmann::json SerializeRecipe(const RawDevelopmentRecipe& recipe) {
    const Raw::RawMosaicDenoiseSettings mosaicDenoise =
        SanitizeMosaicDenoiseSettings(recipe.technical.mosaicDenoise);
    const RawRgbDenoiseRecipe rgbDenoise =
        SanitizeRgbDenoiseRecipe(recipe.rgbDenoise);
    nlohmann::json tonePoints = nlohmann::json::array();
    for (const RawToneCurvePoint& point : recipe.toneCurve.points) {
        tonePoints.push_back({
            { "input", point.input },
            { "output", point.output }
        });
    }

    return {
        { "rawRecipeVersion", kRawDevelopmentRecipeVersion },
        { "processing", {
            { "version", ProcessingVersionStableString(recipe.technical.processingVersion) },
            { "demosaic", DemosaicMethodStableString(recipe.technical.demosaicMethod) },
            { "workingSpace", WorkingSpaceStableString(recipe.technical.workingSpace) },
            { "applyBaselineExposure", recipe.technical.applyBaselineExposure },
            { "outputTransfer",
                recipe.viewTransform.layerJson.value(
                    "encodeSrgbOutput",
                    recipe.technical.encodeSrgbOutput)
                    ? "srgb"
                    : "linear" },
            { "mosaicDenoise", {
                { "enabled", mosaicDenoise.enabled },
                { "mode", MosaicDenoiseModeStableString(
                    mosaicDenoise.mode) },
                { "hotPixelSuppression",
                    mosaicDenoise.hotPixelSuppression },
                { "hotPixelThreshold",
                    mosaicDenoise.hotPixelThreshold },
                { "greenPlaneStrength",
                    mosaicDenoise.lumaStrength },
                { "redBluePlaneStrength",
                    mosaicDenoise.chromaStrength },
                { "radius", mosaicDenoise.radius },
                { "edgeProtection",
                    mosaicDenoise.edgeProtection },
                { "iterations", mosaicDenoise.iterations }
            } }
        } },
        { "sourceRef", {
            { "sourcePath", recipe.source.sourcePath },
            { "relativePathKey", recipe.source.relativePathKey },
            { "fingerprint", recipe.source.fingerprint },
            { "fileSizeBytes", recipe.source.fileSizeBytes },
            { "modifiedTimeTicks", recipe.source.modifiedTimeTicks },
            { "displayName", recipe.source.displayName }
        } },
        { "whiteBalance", {
            { "mode", WhiteBalanceModeStableString(recipe.whiteBalance.mode) },
            { "hasTemperatureKelvin", recipe.whiteBalance.hasTemperatureKelvin },
            { "temperatureKelvin", recipe.whiteBalance.temperatureKelvin },
            { "hasTint", recipe.whiteBalance.hasTint },
            { "tint", recipe.whiteBalance.tint },
            { "hasMultipliers", recipe.whiteBalance.hasMultipliers },
            { "multipliers", recipe.whiteBalance.multipliers },
            { "hasSamplePoint", recipe.whiteBalance.hasSamplePoint },
            { "sampleX", recipe.whiteBalance.sampleX },
            { "sampleY", recipe.whiteBalance.sampleY }
        } },
        { "rgbDenoise", {
            { "version", 2 },
            { "enabled", rgbDenoise.enabled },
            { "method", RgbDenoiseMethodStableString(rgbDenoise.method) },
            { "mapping", RgbDenoiseMappingStableString(rgbDenoise.mapping) },
            { "packageId", rgbDenoise.packageId },
            { "packageVersion", rgbDenoise.packageVersion },
            { "modelSha256", rgbDenoise.modelSha256 },
            { "adapterVersion", rgbDenoise.adapterVersion },
            { "colorNoise", rgbDenoise.colorNoise },
            { "luminanceNoise", rgbDenoise.luminanceNoise },
            { "detailProtection", rgbDenoise.detailProtection }
        } },
        { "exposureEv", recipe.preToneExposureEv },
        { "localExposure", {
            { "enabled", recipe.localExposure.enabled },
            { "amount", recipe.localExposure.amount },
            { "shadowLiftEv", recipe.localExposure.shadowLiftEv },
            { "highlightCompressionEv", recipe.localExposure.highlightCompressionEv },
            { "localBaselineEv", recipe.localExposure.localBaselineEv },
            { "noiseGuardBias", recipe.localExposure.noiseGuardBias },
            { "highlightGuardBias", recipe.localExposure.highlightGuardBias },
            { "shadowGuardBias", recipe.localExposure.shadowGuardBias },
            { "smoothGradientProtection", recipe.localExposure.smoothGradientProtection },
            { "haloGuard", recipe.localExposure.haloGuard }
        } },
        { "localRange", LocalRangeJson(recipe.localRange) },
        { "toneCurve", {
            { "mode", ToneCurveModeStableString(recipe.toneCurve.mode) },
            { "points", tonePoints }
        } },
        { "finishTone", recipe.finishTone.layerJson.is_object()
            ? recipe.finishTone.layerJson
            : DefaultFinishToneJson() },
        { "viewTransform", recipe.viewTransform.layerJson.is_object()
            ? recipe.viewTransform.layerJson
            : DefaultViewTransformJson() },
        { "cropRotate", {
            { "cropEnabled", recipe.cropRotation.cropEnabled },
            { "cropRect", {
                { "x", recipe.cropRotation.cropX },
                { "y", recipe.cropRotation.cropY },
                { "width", recipe.cropRotation.cropWidth },
                { "height", recipe.cropRotation.cropHeight }
            } },
            { "userRotationDegrees", recipe.cropRotation.rotationDegrees },
            { "flipHorizontally", recipe.cropRotation.flipHorizontally },
            { "flipVertically", recipe.cropRotation.flipVertically }
        } },
        { "previewOutput", {
            { "intent", recipe.previewOutput.previewIntent },
            { "internalViewTransform", recipe.previewOutput.internalViewTransform },
            { "outputColorSpace", recipe.previewOutput.outputColorSpace }
        } },
        { "stageOrder", recipe.stageOrder.empty() ? DefaultStageOrder() : recipe.stageOrder }
    };
}

RawDevelopmentRecipe DeserializeRecipe(const nlohmann::json& value) {
    RawDevelopmentRecipe recipe = MakeDefaultRecipe({});
    if (!value.is_object()) {
        return recipe;
    }

    const int storedRecipeVersion = value.value("rawRecipeVersion", 0);
    recipe.rawRecipeVersion = kRawDevelopmentRecipeVersion;
    if (storedRecipeVersion < 7) {
        recipe.technical.processingVersion = Raw::RawProcessingVersion::LegacyV1;
        recipe.technical.demosaicMethod = Raw::DemosaicMethod::Bilinear;
        recipe.technical.workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
        recipe.technical.applyBaselineExposure = false;
        recipe.technical.encodeSrgbOutput = false;
    } else {
        const nlohmann::json processing = value.value("processing", nlohmann::json::object());
        if (processing.is_object()) {
            recipe.technical.processingVersion = ProcessingVersionFromStableString(
                processing.value(
                    "version",
                    std::string(ProcessingVersionStableString(recipe.technical.processingVersion))));
            recipe.technical.demosaicMethod = DemosaicMethodFromStableString(
                processing.value(
                    "demosaic",
                    std::string(DemosaicMethodStableString(recipe.technical.demosaicMethod))));
            recipe.technical.workingSpace = WorkingSpaceFromStableString(
                processing.value(
                    "workingSpace",
                    std::string(WorkingSpaceStableString(recipe.technical.workingSpace))));
            recipe.technical.applyBaselineExposure =
                processing.value("applyBaselineExposure", recipe.technical.applyBaselineExposure);
            recipe.technical.encodeSrgbOutput =
                processing.value("outputTransfer", std::string("srgb")) == "srgb";
            const nlohmann::json mosaicDenoise =
                processing.value("mosaicDenoise", nlohmann::json::object());
            if (mosaicDenoise.is_object()) {
                recipe.technical.mosaicDenoise.enabled =
                    mosaicDenoise.value(
                        "enabled",
                        recipe.technical.mosaicDenoise.enabled);
                recipe.technical.mosaicDenoise.mode =
                    MosaicDenoiseModeFromStableString(
                        mosaicDenoise.value(
                            "mode",
                            std::string(MosaicDenoiseModeStableString(
                                recipe.technical.mosaicDenoise.mode))));
                recipe.technical.mosaicDenoise.hotPixelSuppression =
                    mosaicDenoise.value(
                        "hotPixelSuppression",
                        recipe.technical.mosaicDenoise.hotPixelSuppression);
                recipe.technical.mosaicDenoise.hotPixelThreshold =
                    JsonFloat(
                        mosaicDenoise,
                        "hotPixelThreshold",
                        recipe.technical.mosaicDenoise.hotPixelThreshold);
                recipe.technical.mosaicDenoise.lumaStrength =
                    JsonFloat(
                        mosaicDenoise,
                        "greenPlaneStrength",
                        recipe.technical.mosaicDenoise.lumaStrength);
                recipe.technical.mosaicDenoise.chromaStrength =
                    JsonFloat(
                        mosaicDenoise,
                        "redBluePlaneStrength",
                        recipe.technical.mosaicDenoise.chromaStrength);
                recipe.technical.mosaicDenoise.radius =
                    JsonInteger(
                        mosaicDenoise,
                        "radius",
                        recipe.technical.mosaicDenoise.radius);
                recipe.technical.mosaicDenoise.edgeProtection =
                    JsonFloat(
                        mosaicDenoise,
                        "edgeProtection",
                        recipe.technical.mosaicDenoise.edgeProtection);
                recipe.technical.mosaicDenoise.iterations =
                    JsonInteger(
                        mosaicDenoise,
                        "iterations",
                        recipe.technical.mosaicDenoise.iterations);
            }
        }
    }
    recipe.technical.mosaicDenoise =
        SanitizeMosaicDenoiseSettings(recipe.technical.mosaicDenoise);

    const nlohmann::json source = value.contains("sourceRef")
        ? value.value("sourceRef", nlohmann::json::object())
        : value.value("source", nlohmann::json::object());
    if (source.is_object()) {
        recipe.source.sourcePath = source.value("sourcePath", recipe.source.sourcePath);
        recipe.source.relativePathKey = source.value("relativePathKey", recipe.source.relativePathKey);
        recipe.source.fingerprint = source.value("fingerprint", recipe.source.fingerprint);
        recipe.source.fileSizeBytes = JsonUInt64(source, "fileSizeBytes", recipe.source.fileSizeBytes);
        recipe.source.modifiedTimeTicks = JsonInt64(source, "modifiedTimeTicks", recipe.source.modifiedTimeTicks);
        recipe.source.displayName = source.value("displayName", recipe.source.displayName);
    } else {
        recipe.source.sourcePath = value.value("sourcePath", recipe.source.sourcePath);
    }
    if (recipe.source.relativePathKey.empty()) {
        recipe.source.relativePathKey = recipe.source.sourcePath;
    }
    if (recipe.source.displayName.empty()) {
        recipe.source.displayName = FileNameFromPath(recipe.source.sourcePath);
    }

    const nlohmann::json whiteBalance = value.value("whiteBalance", nlohmann::json::object());
    if (whiteBalance.is_object()) {
        recipe.whiteBalance.mode = WhiteBalanceModeFromStableString(
            whiteBalance.value("mode", std::string(WhiteBalanceModeStableString(recipe.whiteBalance.mode))));
        recipe.whiteBalance.hasTemperatureKelvin = whiteBalance.value("hasTemperatureKelvin", recipe.whiteBalance.hasTemperatureKelvin);
        recipe.whiteBalance.temperatureKelvin = JsonFloat(whiteBalance, "temperatureKelvin", recipe.whiteBalance.temperatureKelvin);
        recipe.whiteBalance.hasTint = whiteBalance.value("hasTint", recipe.whiteBalance.hasTint);
        recipe.whiteBalance.tint = JsonFloat(whiteBalance, "tint", recipe.whiteBalance.tint);
        recipe.whiteBalance.hasMultipliers = whiteBalance.value("hasMultipliers", recipe.whiteBalance.hasMultipliers);
        const nlohmann::json multipliers = whiteBalance.value("multipliers", nlohmann::json::array());
        if (multipliers.is_array() && multipliers.size() >= 3) {
            recipe.whiteBalance.multipliers[0] = multipliers[0].get<float>();
            recipe.whiteBalance.multipliers[1] = multipliers[1].get<float>();
            recipe.whiteBalance.multipliers[2] = multipliers[2].get<float>();
        }
        recipe.whiteBalance.hasSamplePoint = whiteBalance.value("hasSamplePoint", recipe.whiteBalance.hasSamplePoint);
        recipe.whiteBalance.sampleX = JsonFloat(whiteBalance, "sampleX", recipe.whiteBalance.sampleX);
        recipe.whiteBalance.sampleY = JsonFloat(whiteBalance, "sampleY", recipe.whiteBalance.sampleY);
    }

    const nlohmann::json rgbDenoise =
        value.value("rgbDenoise", nlohmann::json::object());
    if (rgbDenoise.is_object()) {
        recipe.rgbDenoise.enabled =
            rgbDenoise.value("enabled", recipe.rgbDenoise.enabled);
        recipe.rgbDenoise.method = RgbDenoiseMethodFromStableString(
            rgbDenoise.value(
                "method",
                std::string(RgbDenoiseMethodStableString(recipe.rgbDenoise.method))));
        recipe.rgbDenoise.mapping = RgbDenoiseMappingFromStableString(
            rgbDenoise.value(
                "mapping",
                std::string(RgbDenoiseMappingStableString(recipe.rgbDenoise.mapping))));
        recipe.rgbDenoise.packageId =
            rgbDenoise.value("packageId", recipe.rgbDenoise.packageId);
        recipe.rgbDenoise.packageVersion =
            rgbDenoise.value("packageVersion", recipe.rgbDenoise.packageVersion);
        recipe.rgbDenoise.modelSha256 =
            rgbDenoise.value("modelSha256", recipe.rgbDenoise.modelSha256);
        recipe.rgbDenoise.adapterVersion =
            rgbDenoise.value("adapterVersion", recipe.rgbDenoise.adapterVersion);
        recipe.rgbDenoise.colorNoise =
            JsonFloat(rgbDenoise, "colorNoise", recipe.rgbDenoise.colorNoise);
        recipe.rgbDenoise.luminanceNoise =
            JsonFloat(rgbDenoise, "luminanceNoise", recipe.rgbDenoise.luminanceNoise);
        recipe.rgbDenoise.detailProtection =
            JsonFloat(rgbDenoise, "detailProtection", recipe.rgbDenoise.detailProtection);
    }
    if (storedRecipeVersion < 12 &&
        recipe.rgbDenoise.adapterVersion == "restormer-rgb-adapter-v1") {
        recipe.rgbDenoise.adapterVersion =
            kRestormerDenoiseAdapterVersion;
        recipe.rgbDenoise.packageVersion.clear();
        recipe.rgbDenoise.modelSha256.clear();
    }
    recipe.rgbDenoise = SanitizeRgbDenoiseRecipe(recipe.rgbDenoise);

    recipe.preToneExposureEv = value.contains("exposureEv")
        ? JsonFloat(value, "exposureEv", recipe.preToneExposureEv)
        : JsonFloat(value, "preToneExposureEv", recipe.preToneExposureEv);

    const nlohmann::json localExposure = value.value("localExposure", nlohmann::json::object());
    if (localExposure.is_object()) {
        recipe.localExposure.enabled = localExposure.value("enabled", recipe.localExposure.enabled);
        recipe.localExposure.amount = JsonFloat(localExposure, "amount", recipe.localExposure.amount);
        recipe.localExposure.shadowLiftEv = JsonFloat(localExposure, "shadowLiftEv", recipe.localExposure.shadowLiftEv);
        recipe.localExposure.highlightCompressionEv =
            JsonFloat(localExposure, "highlightCompressionEv", recipe.localExposure.highlightCompressionEv);
        recipe.localExposure.localBaselineEv = JsonFloat(localExposure, "localBaselineEv", recipe.localExposure.localBaselineEv);
        recipe.localExposure.noiseGuardBias = JsonFloat(localExposure, "noiseGuardBias", recipe.localExposure.noiseGuardBias);
        recipe.localExposure.highlightGuardBias =
            JsonFloat(localExposure, "highlightGuardBias", recipe.localExposure.highlightGuardBias);
        recipe.localExposure.shadowGuardBias = JsonFloat(localExposure, "shadowGuardBias", recipe.localExposure.shadowGuardBias);
        recipe.localExposure.smoothGradientProtection =
            JsonFloat(localExposure, "smoothGradientProtection", recipe.localExposure.smoothGradientProtection);
        recipe.localExposure.haloGuard = JsonFloat(localExposure, "haloGuard", recipe.localExposure.haloGuard);
    }
    recipe.localExposure = SanitizeLocalExposureRecipe(recipe.localExposure);

    const nlohmann::json localRange = value.value("localRange", nlohmann::json::object());
    if (localRange.is_object()) {
        recipe.localRange.enabled = localRange.value("enabled", recipe.localRange.enabled);
        recipe.localRange.strength = JsonFloat(localRange, "strength", recipe.localRange.strength);
        recipe.localRange.middleGrey = JsonFloat(localRange, "middleGrey", recipe.localRange.middleGrey);
        recipe.localRange.minEv = JsonFloat(localRange, "minEv", recipe.localRange.minEv);
        recipe.localRange.maxEv = JsonFloat(localRange, "maxEv", recipe.localRange.maxEv);
        recipe.localRange.smoothness = JsonFloat(localRange, "smoothness", recipe.localRange.smoothness);
        recipe.localRange.edgeProtection = JsonFloat(localRange, "edgeProtection", recipe.localRange.edgeProtection);
        recipe.localRange.detailProtection = JsonFloat(localRange, "detailProtection", recipe.localRange.detailProtection);
        recipe.localRange.highlightProtection = JsonFloat(localRange, "highlightProtection", recipe.localRange.highlightProtection);
        recipe.localRange.maskPreviewMode = localRange.value("maskPreviewMode", recipe.localRange.maskPreviewMode);
        recipe.localRange.regionMaskEnabled = localRange.value("regionMaskEnabled", recipe.localRange.regionMaskEnabled);
        recipe.localRange.regionMaskMode = localRange.value("regionMaskMode", recipe.localRange.regionMaskMode);
        recipe.localRange.regionMaskInvert = localRange.value("regionMaskInvert", recipe.localRange.regionMaskInvert);
        recipe.localRange.regionMaskCenterX = JsonFloat(localRange, "regionMaskCenterX", recipe.localRange.regionMaskCenterX);
        recipe.localRange.regionMaskCenterY = JsonFloat(localRange, "regionMaskCenterY", recipe.localRange.regionMaskCenterY);
        recipe.localRange.regionMaskAngleDegrees =
            JsonFloat(localRange, "regionMaskAngleDegrees", recipe.localRange.regionMaskAngleDegrees);
        recipe.localRange.regionMaskSize = JsonFloat(localRange, "regionMaskSize", recipe.localRange.regionMaskSize);
        recipe.localRange.regionMaskFeather = JsonFloat(localRange, "regionMaskFeather", recipe.localRange.regionMaskFeather);
        recipe.localRange.regionMaskLowEv = JsonFloat(localRange, "regionMaskLowEv", recipe.localRange.regionMaskLowEv);
        recipe.localRange.regionMaskHighEv = JsonFloat(localRange, "regionMaskHighEv", recipe.localRange.regionMaskHighEv);
        recipe.localRange.colorMaskEnabled = localRange.value("colorMaskEnabled", recipe.localRange.colorMaskEnabled);
        recipe.localRange.colorMaskTargetR = JsonFloat(localRange, "colorMaskTargetR", recipe.localRange.colorMaskTargetR);
        recipe.localRange.colorMaskTargetG = JsonFloat(localRange, "colorMaskTargetG", recipe.localRange.colorMaskTargetG);
        recipe.localRange.colorMaskTargetB = JsonFloat(localRange, "colorMaskTargetB", recipe.localRange.colorMaskTargetB);
        recipe.localRange.colorMaskHueWidth = JsonFloat(localRange, "colorMaskHueWidth", recipe.localRange.colorMaskHueWidth);
        recipe.localRange.colorMaskFeather = JsonFloat(localRange, "colorMaskFeather", recipe.localRange.colorMaskFeather);
        recipe.localRange.colorMaskMinChroma = JsonFloat(localRange, "colorMaskMinChroma", recipe.localRange.colorMaskMinChroma);
        recipe.localRange.targetZoneCombineMode = LocalRangeZoneCombineModeFromStableString(
            localRange.value(
                "targetZoneCombineMode",
                std::string(LocalRangeZoneCombineModeStableString(
                    recipe.localRange.targetZoneCombineMode))));
        recipe.localRange.targetZones.clear();
        const nlohmann::json targetZones =
            localRange.value("targetZones", nlohmann::json::array());
        if (targetZones.is_array()) {
            for (const nlohmann::json& item : targetZones) {
                if (!item.is_object()) {
                    continue;
                }
                RawLocalRangeTargetZone zone;
                zone.id = item.value("id", std::string());
                zone.name = item.value("name", std::string());
                zone.enabled = item.value("enabled", zone.enabled);
                zone.centerEv = JsonFloat(item, "centerEv", zone.centerEv);
                zone.coreHalfWidthEv =
                    JsonFloat(item, "coreHalfWidthEv", zone.coreHalfWidthEv);
                zone.featherEv = JsonFloat(item, "featherEv", zone.featherEv);
                zone.deltaEv = JsonFloat(item, "deltaEv", zone.deltaEv);
                zone.scope = LocalRangeTargetScopeFromStableString(
                    item.value(
                        "scope",
                        std::string(LocalRangeTargetScopeStableString(zone.scope))));
                zone.colorEnabled = item.value("colorEnabled", zone.colorEnabled);
                zone.targetUPrime = JsonFloat(item, "targetUPrime", zone.targetUPrime);
                zone.targetVPrime = JsonFloat(item, "targetVPrime", zone.targetVPrime);
                zone.targetChroma = JsonFloat(item, "targetChroma", zone.targetChroma);
                zone.colorRadius = JsonFloat(item, "colorRadius", zone.colorRadius);
                zone.colorFeather = JsonFloat(item, "colorFeather", zone.colorFeather);
                const nlohmann::json seeds = item.value("seeds", nlohmann::json::array());
                if (seeds.is_array()) {
                    for (const nlohmann::json& seed : seeds) {
                        if (!seed.is_object()) {
                            continue;
                        }
                        zone.seeds.push_back({
                            JsonFloat(seed, "sourceU", 0.5f),
                            JsonFloat(seed, "sourceV", 0.5f)
                        });
                    }
                }
                recipe.localRange.targetZones.push_back(std::move(zone));
            }
        }
        recipe.localRange.points.clear();
        const nlohmann::json points = localRange.value("points", nlohmann::json::array());
        if (points.is_array()) {
            for (const nlohmann::json& item : points) {
                if (!item.is_object()) {
                    continue;
                }
                recipe.localRange.points.push_back({
                    JsonFloat(item, "ev", 0.0f),
                    JsonFloat(item, "deltaEv", 0.0f)
                });
            }
        }
    }
    recipe.localRange = SanitizeLocalRangeRecipe(recipe.localRange);

    const nlohmann::json toneCurve = value.value("toneCurve", nlohmann::json::object());
    if (toneCurve.is_object()) {
        recipe.toneCurve.mode = ToneCurveModeFromStableString(
            toneCurve.value("mode", std::string(ToneCurveModeStableString(recipe.toneCurve.mode))));
        recipe.toneCurve.points.clear();
        const nlohmann::json points = toneCurve.value("points", nlohmann::json::array());
        if (points.is_array()) {
            for (const nlohmann::json& item : points) {
                if (!item.is_object()) {
                    continue;
                }
                recipe.toneCurve.points.push_back({
                    JsonFloat(item, "input", 0.0f),
                    JsonFloat(item, "output", 0.0f)
                });
            }
        }
    }
    if (recipe.toneCurve.points.empty()) {
        recipe.toneCurve.points = DefaultToneCurvePoints();
    }
    const nlohmann::json finishTone = value.value("finishTone", nlohmann::json::object());
    recipe.finishTone.layerJson = storedRecipeVersion < 3
        ? SanitizeFinishToneJson(
              FinishToneJsonFromLegacyToneCurve(recipe.toneCurve),
              recipe.toneCurve,
              storedRecipeVersion)
        : SanitizeFinishToneJson(finishTone, recipe.toneCurve, storedRecipeVersion);

    const nlohmann::json viewTransform = value.value("viewTransform", nlohmann::json::object());
    recipe.viewTransform.layerJson = SanitizeViewTransformJson(viewTransform);
    if (storedRecipeVersion < 7) {
        recipe.viewTransform.layerJson["inputWorkingSpace"] = "linear-srgb-d65";
        recipe.viewTransform.layerJson["encodeSrgbOutput"] = false;
    } else {
        recipe.viewTransform.layerJson["inputWorkingSpace"] =
            WorkingSpaceStableString(recipe.technical.workingSpace);
        recipe.viewTransform.layerJson["encodeSrgbOutput"] = recipe.technical.encodeSrgbOutput;
    }

    const nlohmann::json cropRotation = value.contains("cropRotate")
        ? value.value("cropRotate", nlohmann::json::object())
        : value.value("cropRotation", nlohmann::json::object());
    if (cropRotation.is_object()) {
        recipe.cropRotation.cropEnabled = cropRotation.value("cropEnabled", recipe.cropRotation.cropEnabled);
        const nlohmann::json cropRect = cropRotation.value("cropRect", nlohmann::json::object());
        if (cropRect.is_object()) {
            recipe.cropRotation.cropX = JsonFloat(cropRect, "x", recipe.cropRotation.cropX);
            recipe.cropRotation.cropY = JsonFloat(cropRect, "y", recipe.cropRotation.cropY);
            recipe.cropRotation.cropWidth = JsonFloat(cropRect, "width", recipe.cropRotation.cropWidth);
            recipe.cropRotation.cropHeight = JsonFloat(cropRect, "height", recipe.cropRotation.cropHeight);
        } else {
            recipe.cropRotation.cropX = JsonFloat(cropRotation, "cropX", recipe.cropRotation.cropX);
            recipe.cropRotation.cropY = JsonFloat(cropRotation, "cropY", recipe.cropRotation.cropY);
            recipe.cropRotation.cropWidth = JsonFloat(cropRotation, "cropWidth", recipe.cropRotation.cropWidth);
            recipe.cropRotation.cropHeight = JsonFloat(cropRotation, "cropHeight", recipe.cropRotation.cropHeight);
        }
        recipe.cropRotation.rotationDegrees = cropRotation.value(
            "userRotationDegrees",
            cropRotation.value("rotationDegrees", recipe.cropRotation.rotationDegrees));
        recipe.cropRotation.flipHorizontally = cropRotation.value(
            "flipHorizontally",
            recipe.cropRotation.flipHorizontally);
        recipe.cropRotation.flipVertically = cropRotation.value(
            "flipVertically",
            recipe.cropRotation.flipVertically);
    }

    const nlohmann::json previewOutput = value.value("previewOutput", nlohmann::json::object());
    if (previewOutput.is_object()) {
        recipe.previewOutput.previewIntent = previewOutput.value(
            "intent",
            previewOutput.value("previewIntent", recipe.previewOutput.previewIntent));
        recipe.previewOutput.internalViewTransform = previewOutput.value("internalViewTransform", recipe.previewOutput.internalViewTransform);
        recipe.previewOutput.outputColorSpace = previewOutput.value("outputColorSpace", recipe.previewOutput.outputColorSpace);
    }
    if (recipe.technical.processingVersion == Raw::RawProcessingVersion::TruthfulV1) {
        recipe.previewOutput.outputColorSpace = "sRGB";
    }

    recipe.stageOrder.clear();
    const nlohmann::json stageOrder = value.value("stageOrder", nlohmann::json::array());
    if (stageOrder.is_array()) {
        for (const nlohmann::json& stage : stageOrder) {
            if (stage.is_string()) {
                recipe.stageOrder.push_back(stage.get<std::string>());
            }
        }
    }
    if (recipe.stageOrder.empty()) {
        recipe.stageOrder = DefaultStageOrder();
    } else {
        recipe.stageOrder = NormalizeStageOrder(recipe.stageOrder);
    }

    return recipe;
}

std::string RecipeDisplayName(const RawDevelopmentRecipe& recipe) {
    if (!recipe.source.displayName.empty()) {
        return recipe.source.displayName;
    }
    return FileNameFromPath(recipe.source.sourcePath);
}

} // namespace Stack::RawRecipe
