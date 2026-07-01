#include "Raw/RawDevelopmentRecipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>

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

nlohmann::json SanitizeFinishToneJson(nlohmann::json value, const RawToneCurveRecipe& legacyToneCurve) {
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
    return value;
}

nlohmann::json SanitizeViewTransformJson(nlohmann::json value) {
    if (!value.is_object()) {
        value = DefaultViewTransformJson();
    }
    value["type"] = "ViewTransform";
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
        { "colorMaskMinChroma", localRange.colorMaskMinChroma }
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

std::uint64_t JsonUInt64(const nlohmann::json& value, const char* key, std::uint64_t fallback) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_unsigned()) {
        return fallback;
    }
    return it->get<std::uint64_t>();
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

nlohmann::json DefaultFinishToneJson() {
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
        { "logMinEv", -10.0f },
        { "logMaxEv", 6.0f },
        { "middleGrey", 0.18f }
    };
}

nlohmann::json DefaultViewTransformJson() {
    return {
        { "type", "ViewTransform" },
        { "exposure", 0.0f },
        { "blackEv", -8.0f },
        { "whiteEv", 4.0f },
        { "middleGrey", 0.18f },
        { "shoulder", 0.45f },
        { "toe", 0.18f },
        { "contrast", 1.0f },
        { "saturation", 1.0f },
        { "preserveHue", true },
        { "debugFalseColor", false }
    };
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
    return finishTone;
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

RawLocalRangeRecipe ApplyLocalRangePreset(RawLocalRangeRecipe localRange, RawLocalRangePreset preset) {
    if (preset == RawLocalRangePreset::Reset) {
        return DefaultLocalRangeRecipe();
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

float EvaluateLocalRangeDeltaEv(const RawLocalRangeRecipe& localRange, float sceneEv) {
    const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(localRange);
    if (!sanitized.enabled || sanitized.strength <= 0.0001f || sanitized.points.size() < 2 || !std::isfinite(sceneEv)) {
        return 0.0f;
    }

    const float clampedEv = std::clamp(sceneEv, sanitized.minEv, sanitized.maxEv);
    RawLocalRangePoint previous = sanitized.points.front();
    if (clampedEv <= previous.ev) {
        return sanitized.strength * previous.deltaEv;
    }

    for (std::size_t i = 1; i < sanitized.points.size(); ++i) {
        const RawLocalRangePoint current = sanitized.points[i];
        if (clampedEv <= current.ev) {
            const float span = std::max(current.ev - previous.ev, 0.0001f);
            const float t = std::clamp((clampedEv - previous.ev) / span, 0.0f, 1.0f);
            return sanitized.strength * (previous.deltaEv + (current.deltaEv - previous.deltaEv) * t);
        }
        previous = current;
    }

    return sanitized.strength * previous.deltaEv;
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
    return false;
}

bool IsLocalRangeEnabled(const RawDevelopmentRecipe& recipe) {
    return IsLocalRangeEnabled(recipe.localRange);
}

nlohmann::json SerializeRecipe(const RawDevelopmentRecipe& recipe) {
    nlohmann::json tonePoints = nlohmann::json::array();
    for (const RawToneCurvePoint& point : recipe.toneCurve.points) {
        tonePoints.push_back({
            { "input", point.input },
            { "output", point.output }
        });
    }

    return {
        { "rawRecipeVersion", kRawDevelopmentRecipeVersion },
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
            { "userRotationDegrees", recipe.cropRotation.rotationDegrees }
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

    const int storedRecipeVersion = value.value("rawRecipeVersion", recipe.rawRecipeVersion);
    recipe.rawRecipeVersion = kRawDevelopmentRecipeVersion;

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
        ? SanitizeFinishToneJson(FinishToneJsonFromLegacyToneCurve(recipe.toneCurve), recipe.toneCurve)
        : SanitizeFinishToneJson(finishTone, recipe.toneCurve);

    const nlohmann::json viewTransform = value.value("viewTransform", nlohmann::json::object());
    recipe.viewTransform.layerJson = SanitizeViewTransformJson(viewTransform);

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
    }

    const nlohmann::json previewOutput = value.value("previewOutput", nlohmann::json::object());
    if (previewOutput.is_object()) {
        recipe.previewOutput.previewIntent = previewOutput.value(
            "intent",
            previewOutput.value("previewIntent", recipe.previewOutput.previewIntent));
        recipe.previewOutput.internalViewTransform = previewOutput.value("internalViewTransform", recipe.previewOutput.internalViewTransform);
        recipe.previewOutput.outputColorSpace = previewOutput.value("outputColorSpace", recipe.previewOutput.outputColorSpace);
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
