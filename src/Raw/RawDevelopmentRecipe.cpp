#include "HdrDisplayMapping.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawZoneArea.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <unordered_set>

namespace Stack::RawRecipe {
namespace {

constexpr std::size_t kMaxRawToneCurvePoints = 12;
constexpr std::size_t kMaxRawLocalRangePoints = 12;

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

nlohmann::json SerializeBezierHandle(const RawBezierHandleState& input);
float JsonFloat(const nlohmann::json& value, const char* key, float fallback);

bool HasPreparedColorWarpEffect(const RawColorWarpRecipe& colorWarp) {
    if (!colorWarp.enabled || colorWarp.strength <= 0.0001f) {
        return false;
    }
    return std::any_of(
        colorWarp.pins.begin(),
        colorWarp.pins.end(),
        [](const RawColorWarpPin& pin) {
            return pin.enabled && pin.strength > 0.0001f &&
                (pin.protectColor ||
                    std::abs(pin.targetA - pin.sourceA) > 0.000001f ||
                    std::abs(pin.targetB - pin.sourceB) > 0.000001f ||
                    std::abs(pin.lightnessDeltaEv) > 0.000001f);
        });
}

Raw::RawMosaicDenoiseSettings SanitizeMosaicDenoiseSettings(
    Raw::RawMosaicDenoiseSettings settings) {
    const Raw::RawMosaicDenoiseSettings defaults;
    if (settings.mode != Raw::RawMosaicDenoiseMode::FixedThreshold &&
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

nlohmann::json SanitizeFinishToneJson(nlohmann::json value) {
    const nlohmann::json input = value.is_object()
        ? std::move(value)
        : nlohmann::json::object();
    value = DefaultFinishToneJson();
    for (auto it = value.begin(); it != value.end(); ++it) {
        const auto inputValue = input.find(it.key());
        if (inputValue != input.end()) {
            it.value() = *inputValue;
        }
    }
    value["type"] = "ToneCurve";
    value["mode"] = std::clamp(value.value("mode", 1), 0, 4);
    value["domain"] = std::clamp(value.value("domain", 1), 0, 1);
    value["activeGraphView"] = std::clamp(value.value("activeGraphView", 0), 0, 1);
    value["logMinEv"] = ClampFinite(value.value("logMinEv", -10.0f), -10.0f, -20.0f, 0.0f);
    value["logMaxEv"] = ClampFinite(value.value("logMaxEv", 6.0f), 6.0f, 0.0f, 20.0f);
    value["middleGrey"] = ClampFinite(value.value("middleGrey", 0.18f), 0.18f, 0.01f, 1.0f);
    value["curveRangeMode"] = kFinishToneRangeExtendedSceneV1;
    if (value["logMaxEv"].get<float>() <= value["logMinEv"].get<float>() + 0.1f) {
        value["logMaxEv"] = value["logMinEv"].get<float>() + 0.1f;
    }
    if (!value.contains("points") || !value["points"].is_array() || value["points"].size() < 2) {
        value["points"] = DefaultToneCurveLayerPointsJson();
    }
    if (!value.contains("preparedPoints") || !value["preparedPoints"].is_array() || value["preparedPoints"].size() < 2) {
        value["preparedPoints"] = value["points"];
    }
    return SanitizeFinishTonePointCurveJson(std::move(value));
}

nlohmann::json SanitizeViewTransformJson(nlohmann::json value) {
    const nlohmann::json input = value.is_object()
        ? std::move(value)
        : nlohmann::json::object();
    value = DefaultViewTransformJson();
    for (auto it = value.begin(); it != value.end(); ++it) {
        const auto inputValue = input.find(it.key());
        if (inputValue != input.end()) {
            it.value() = *inputValue;
        }
    }
    value["type"] = "ViewTransform";
    value["displayCurve"] = value.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic
        ? Raw::HdrDisplay::Photographic : "standard";
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
    value["contrastPivotEv"] = ClampFinite(
        value.value("contrastPivotEv", 0.0f),
        0.0f,
        -8.0f,
        8.0f);
    value["contrastModel"] = kViewContrastModelPivotedLogV2;
    const float minimumPivot = std::max(
        -8.0f,
        value["blackEv"].get<float>() + 0.1f);
    const float maximumPivot = std::min(
        8.0f,
        value["whiteEv"].get<float>() - 0.1f);
    if (minimumPivot <= maximumPivot) {
        value["contrastPivotEv"] = std::clamp(
            value["contrastPivotEv"].get<float>(),
            minimumPivot,
            maximumPivot);
    }
    value["saturation"] = ClampFinite(value.value("saturation", 1.0f), 1.0f, 0.0f, 2.0f);
    value["preserveHue"] = value.value("preserveHue", true);
    value["debugFalseColor"] = value.value("debugFalseColor", false);
    value["inputWorkingSpace"] = value.value("inputWorkingSpace", std::string("linear-rec2020-d65"));
    value["encodeSrgbOutput"] = value.value("encodeSrgbOutput", true);
    return value;
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
            { "deltaEv", point.deltaEv },
            { "incomingHandle", SerializeBezierHandle(point.incoming) },
            { "outgoingHandle", SerializeBezierHandle(point.outgoing) }
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
        { "targetZones", std::move(targetZones) },
        { "areasVersion", localRange.areasVersion },
        { "areas", SerializeZoneAreas(localRange.areas) }
    };
}

RawGradientMask SanitizeGradientMask(RawGradientMask mask) {
    mask.geometryVersion = 1;
    mask.centerU = ClampFinite(mask.centerU, 0.5f, -2.0f, 3.0f);
    mask.centerV = ClampFinite(mask.centerV, 0.5f, -2.0f, 3.0f);
    mask.angleRadians = ClampFinite(mask.angleRadians, 0.0f, -100.0f, 100.0f);
    mask.lowBoundary = ClampFinite(mask.lowBoundary, -0.15f, -4.0f, 3.99f);
    mask.highBoundary = ClampFinite(mask.highBoundary, 0.15f,
        mask.lowBoundary + 0.001f, 4.0f);
    mask.radiusX = ClampFinite(mask.radiusX, 0.25f, 0.001f, 4.0f);
    mask.radiusY = ClampFinite(mask.radiusY, 0.25f, 0.001f, 4.0f);
    mask.innerScale = ClampFinite(mask.innerScale, 0.65f, 0.0f, 0.999f);
    return mask;
}

nlohmann::json GradientMaskJson(const RawGradientMask& input) {
    const RawGradientMask mask = SanitizeGradientMask(input);
    return {
        {"id", mask.id}, {"geometryVersion", mask.geometryVersion},
        {"shape", mask.shape == RawGradientShape::Linear ? "linear" : "radial"},
        {"enabled", mask.enabled}, {"inverted", mask.inverted},
        {"centerU", mask.centerU}, {"centerV", mask.centerV},
        {"angleRadians", mask.angleRadians},
        {"lowBoundary", mask.lowBoundary}, {"highBoundary", mask.highBoundary},
        {"radiusX", mask.radiusX}, {"radiusY", mask.radiusY},
        {"innerScale", mask.innerScale}
    };
}

RawGradientMask GradientMaskFromJson(const nlohmann::json& value) {
    RawGradientMask mask;
    if (!value.is_object()) return mask;
    mask.id = value.value("id", std::string{});
    mask.shape = value.value("shape", std::string("linear")) == "radial"
        ? RawGradientShape::Radial : RawGradientShape::Linear;
    mask.enabled = value.value("enabled", true);
    mask.inverted = value.value("inverted", false);
    mask.centerU = JsonFloat(value, "centerU", mask.centerU);
    mask.centerV = JsonFloat(value, "centerV", mask.centerV);
    mask.angleRadians = JsonFloat(value, "angleRadians", mask.angleRadians);
    mask.lowBoundary = JsonFloat(value, "lowBoundary", mask.lowBoundary);
    mask.highBoundary = JsonFloat(value, "highBoundary", mask.highBoundary);
    mask.radiusX = JsonFloat(value, "radiusX", mask.radiusX);
    mask.radiusY = JsonFloat(value, "radiusY", mask.radiusY);
    mask.innerScale = JsonFloat(value, "innerScale", mask.innerScale);
    return SanitizeGradientMask(std::move(mask));
}

nlohmann::json GradientEvJson(const std::vector<RawGradientEvAdjustment>& items) {
    nlohmann::json result = nlohmann::json::array();
    for (std::size_t i = 0; i < std::min(items.size(), kMaxRawGradientAdjustments); ++i) {
        result.push_back({{"mask", GradientMaskJson(items[i].mask)},
            {"curve", LocalRangeJson(items[i].curve)}});
    }
    return result;
}

nlohmann::json GradientToneJson(const std::vector<RawGradientToneAdjustment>& items) {
    nlohmann::json result = nlohmann::json::array();
    for (std::size_t i = 0; i < std::min(items.size(), kMaxRawGradientAdjustments); ++i) {
        result.push_back({{"mask", GradientMaskJson(items[i].mask)},
            {"curve", SanitizeFinishToneJson(items[i].curveJson)}});
    }
    return result;
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

RawColorWarpEvCurve MakeUniformColorWarpEvCurve(float qualification) {
    RawColorWarpEvCurve curve;
    curve.samples.assign(
        kRawColorWarpEvCurveSampleCount,
        std::clamp(std::isfinite(qualification) ? qualification : 1.0f, 0.0f, 1.0f));
    return curve;
}

RawColorWarpEvCurve MakeColorWarpEvCurveHump(
    float centerEv,
    float coreHalfWidthEv,
    float featherEv) {
    centerEv = std::clamp(
        std::isfinite(centerEv) ? centerEv : 0.0f,
        kRawColorWarpEvMinimum,
        kRawColorWarpEvMaximum);
    coreHalfWidthEv = std::clamp(
        std::isfinite(coreHalfWidthEv) ? coreHalfWidthEv : 0.75f,
        0.0f,
        16.0f);
    featherEv = std::clamp(
        std::isfinite(featherEv) ? featherEv : 0.75f,
        0.0f,
        16.0f);
    RawColorWarpEvCurve curve = MakeUniformColorWarpEvCurve(0.0f);
    const float low = centerEv - coreHalfWidthEv;
    const float high = centerEv + coreHalfWidthEv;
    for (std::size_t index = 0; index < curve.samples.size(); ++index) {
        const float t = static_cast<float>(index) /
            static_cast<float>(curve.samples.size() - 1u);
        const float ev = kRawColorWarpEvMinimum +
            t * (kRawColorWarpEvMaximum - kRawColorWarpEvMinimum);
        const float lowWeight = featherEv <= 0.0001f
            ? (ev >= low ? 1.0f : 0.0f)
            : SmoothStep(low - featherEv, low, ev);
        const float highWeight = featherEv <= 0.0001f
            ? (ev <= high ? 1.0f : 0.0f)
            : 1.0f - SmoothStep(high, high + featherEv, ev);
        curve.samples[index] = lowWeight * highWeight;
    }
    return curve;
}

float EvaluateColorWarpEvCurve(
    const RawColorWarpEvCurve& curve,
    float sceneEv) {
    if (curve.samples.empty()) {
        return 1.0f;
    }
    const float normalized = std::clamp(
        (sceneEv - kRawColorWarpEvMinimum) /
            (kRawColorWarpEvMaximum - kRawColorWarpEvMinimum),
        0.0f,
        1.0f);
    const float position = normalized *
        static_cast<float>(curve.samples.size() - 1u);
    const std::size_t lower = std::min(
        curve.samples.size() - 1u,
        static_cast<std::size_t>(std::floor(position)));
    const std::size_t upper = std::min(curve.samples.size() - 1u, lower + 1u);
    const float fraction = position - static_cast<float>(lower);
    const float low = std::clamp(
        std::isfinite(curve.samples[lower]) ? curve.samples[lower] : 1.0f,
        0.0f,
        1.0f);
    const float high = std::clamp(
        std::isfinite(curve.samples[upper]) ? curve.samples[upper] : low,
        0.0f,
        1.0f);
    return low + (high - low) * fraction;
}

const std::vector<std::string>& DefaultStageOrder() {
    static const std::vector<std::string> kOrder = {
        "source",
        "raw-decode",
        "white-balance",
        "rgb-denoise",
        "color-calibration",
        "pre-tone-exposure",
        "local-range",
        "tone-curve",
        "color-warp",
        "detail-contrast",
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
    recipe.finishTone.layerJson = DefaultFinishToneJson();
    recipe.viewTransform.layerJson = DefaultViewTransformJson();
    recipe.stageOrder = DefaultStageOrder();
    return recipe;
}

RawDevelopmentRecipe BuildNeutralComparisonRecipe(
    const RawDevelopmentRecipe& currentRecipe) {
    RawDevelopmentRecipe neutral = MakeDefaultRecipe(
        currentRecipe.source.sourcePath,
        currentRecipe.source.displayName);
    neutral.source = currentRecipe.source;

    // Keep the current technical and color-domain foundation while resetting
    // authored denoise, tone, and color controls.
    neutral.technical.processingVersion =
        Raw::RawProcessingVersion::TruthfulV2;
    neutral.technical.demosaicMethod =
        currentRecipe.technical.demosaicMethod;
    neutral.technical.workingSpace =
        currentRecipe.technical.workingSpace;
    neutral.technical.applyBaselineExposure =
        currentRecipe.technical.applyBaselineExposure;
    neutral.technical.encodeSrgbOutput =
        currentRecipe.technical.encodeSrgbOutput;

    // Crop and orientation define which pixels are being compared. They are
    // deliberately retained even though every creative adjustment is reset.
    neutral.cropRotation = currentRecipe.cropRotation;

    // Neutral Compare is the one deliberate bypass snapshot. Ordinary Color
    // Warp recipes default on, but this comparison removes the entire stage.
    neutral.colorWarp.enabled = false;

    // Preserve internal-vs-graph View placement without preserving a custom
    // authored View curve. Otherwise an external View node could be applied
    // twice in the comparison snapshot.
    const bool currentInternalViewEnabled =
        currentRecipe.viewTransform.layerJson.value("enabled", true);
    neutral.viewTransform.layerJson["enabled"] =
        currentInternalViewEnabled;
    return neutral;
}

const char* WhiteBalanceModeStableString(WhiteBalanceMode mode) {
    switch (mode) {
        case WhiteBalanceMode::AsShot: return "as-shot";
        case WhiteBalanceMode::CustomMultipliers: return "custom-multipliers";
    }
    return "as-shot";
}

WhiteBalanceMode WhiteBalanceModeFromStableString(const std::string& value) {
    if (value == "custom-multipliers") {
        return WhiteBalanceMode::CustomMultipliers;
    }
    return WhiteBalanceMode::AsShot;
}

const char* ProcessingVersionStableString(Raw::RawProcessingVersion version) {
    switch (version) {
        case Raw::RawProcessingVersion::TruthfulV2: return "truthful-v2";
    }
    return "truthful-v2";
}

const char* DemosaicMethodStableString(Raw::DemosaicMethod method) {
    switch (method) {
        case Raw::DemosaicMethod::Bilinear: return "bilinear";
        case Raw::DemosaicMethod::MalvarHeCutler: return "malvar-he-cutler-5x5";
        case Raw::DemosaicMethod::NearestNeighbor: return "nearest-neighbor";
        case Raw::DemosaicMethod::HamiltonAdams: return "hamilton-adams";
    }
    return "bilinear";
}

Raw::DemosaicMethod DemosaicMethodFromStableString(const std::string& value) {
    if (value == "malvar-he-cutler-5x5") {
        return Raw::DemosaicMethod::MalvarHeCutler;
    }
    if (value == "nearest-neighbor") {
        return Raw::DemosaicMethod::NearestNeighbor;
    }
    if (value == "hamilton-adams") {
        return Raw::DemosaicMethod::HamiltonAdams;
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
    if (value == "linear-rec2020-d65") {
        return Raw::RawWorkingSpace::LinearRec2020D65;
    }
    return Raw::RawWorkingSpace::LinearSrgbD65;
}

const char* MosaicDenoiseModeStableString(Raw::RawMosaicDenoiseMode mode) {
    switch (mode) {
        case Raw::RawMosaicDenoiseMode::FixedThreshold:
            return "fixed-threshold";
        case Raw::RawMosaicDenoiseMode::DngNoiseProfile:
            return "dng-noise-profile-v1";
    }
    return "dng-noise-profile-v1";
}

Raw::RawMosaicDenoiseMode MosaicDenoiseModeFromStableString(
    const std::string& value) {
    if (value == "fixed-threshold") {
        return Raw::RawMosaicDenoiseMode::FixedThreshold;
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

RawBezierHandleState SanitizeBezierHandle(
    RawBezierHandleState handle,
    float fallbackStrength = 0.0f) {
    handle.strength = ClampFinite(
        handle.strength,
        fallbackStrength,
        0.0f,
        1.5f);
    handle.offsetX = ClampFinite(handle.offsetX, 0.0f, -1.0f, 1.0f);
    handle.offsetY = ClampFinite(handle.offsetY, 0.0f, -1.0f, 1.0f);
    if (!handle.manual) {
        handle.offsetX = 0.0f;
        handle.offsetY = 0.0f;
    }
    return handle;
}

RawBezierHandleState BezierHandleFromJson(
    const nlohmann::json& value,
    float missingStrength) {
    RawBezierHandleState handle;
    handle.strength = missingStrength;
    if (value.is_object()) {
        handle.strength = JsonFloat(value, "strength", missingStrength);
        handle.manual = value.value("manual", false);
        handle.offsetX = JsonFloat(value, "offsetX", 0.0f);
        handle.offsetY = JsonFloat(value, "offsetY", 0.0f);
    }
    return SanitizeBezierHandle(handle, missingStrength);
}

nlohmann::json SerializeBezierHandle(const RawBezierHandleState& input) {
    const RawBezierHandleState handle = SanitizeBezierHandle(input);
    return {
        { "strength", handle.strength },
        { "manual", handle.manual },
        { "offsetX", handle.offsetX },
        { "offsetY", handle.offsetY }
    };
}

std::vector<RawPointCurveControlPoint> SanitizePointCurvePoints(
    const nlohmann::json& value,
    float missingStrength = 0.0f) {
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
            point.incoming = BezierHandleFromJson(
                item.value("incomingHandle", nlohmann::json::object()),
                missingStrength);
            point.outgoing = BezierHandleFromJson(
                item.value("outgoingHandle", nlohmann::json::object()),
                missingStrength);
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
            { "shape", std::clamp(point.shape, 0, 2) },
            { "incomingHandle", SerializeBezierHandle(point.incoming) },
            { "outgoingHandle", SerializeBezierHandle(point.outgoing) }
        });
    }
    return result;
}

RawPointCurveComponent SanitizePointCurveComponent(
    const nlohmann::json& value) {
    RawPointCurveComponent component;
    const nlohmann::json object = value.is_object() ? value : nlohmann::json::object();
    component.interpolation = "bezier-segments-v1";
    component.points = SanitizePointCurvePoints(
        object.value("points", IdentityPointCurveJson()),
        0.0f);
    if (object.contains("basePoints")) {
        component.basePoints = SanitizePointCurvePoints(
            object["basePoints"],
            0.0f);
    }
    return component;
}

nlohmann::json SerializePointCurveComponent(const RawPointCurveComponent& component) {
    const auto hasOnlyDefaultHandles = [](
        const std::vector<RawPointCurveControlPoint>& points) {
        for (const RawPointCurveControlPoint& point : points) {
            for (const RawBezierHandleState* handle :
                 { &point.incoming, &point.outgoing }) {
                if (handle->manual ||
                    std::abs(handle->strength) > 0.000001f ||
                    std::abs(handle->offsetX) > 0.000001f ||
                    std::abs(handle->offsetY) > 0.000001f) {
                    return false;
                }
            }
        }
        return true;
    };
    const bool compactIdentity =
        component.basePoints.empty() &&
        IsIdentityRawPointCurveComponent(component) &&
        hasOnlyDefaultHandles(component.points);
    nlohmann::json result = {
        { "interpolation", component.interpolation },
        { "points", compactIdentity
            ? nlohmann::json::array()
            : SerializePointCurvePoints(component.points) }
    };
    if (!component.basePoints.empty()) {
        result["basePoints"] = SerializePointCurvePoints(component.basePoints);
    }
    return result;
}

float PointCurveCoordinateFromSceneUnclamped(
    float value,
    int domain,
    float minimumEv,
    float maximumEv,
    float middleGrey) {
    if (domain == 1) {
        const float ev = std::log2(
            std::max(value, std::numeric_limits<float>::min()) / std::max(middleGrey, 0.000001f));
        return (ev - minimumEv) /
            std::max(0.0001f, maximumEv - minimumEv);
    }
    return value;
}

float PointCurveSceneFromCoordinateUnclamped(
    float coordinate,
    int domain,
    float minimumEv,
    float maximumEv,
    float middleGrey) {
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
        { "interpolation", "bezier-segments-v1" },
        { "points", nlohmann::json::array() }
    };
}

nlohmann::json SanitizeFinishTonePointCurveJson(nlohmann::json finishTone) {
    if (!finishTone.is_object()) {
        finishTone = nlohmann::json::object();
    }

    nlohmann::json curveSet =
        finishTone.contains("pointCurves") && finishTone["pointCurves"].is_object()
            ? finishTone["pointCurves"]
            : nlohmann::json::object();
    for (int index = 0; index < 4; ++index) {
        const char* key = RawPointCurveChannelKey(static_cast<RawPointCurveChannel>(index));
        const nlohmann::json componentValue =
            curveSet.value(key, DefaultPointCurveComponentJson());
        const RawPointCurveComponent component =
            SanitizePointCurveComponent(componentValue);
        curveSet[key] = SerializePointCurveComponent(component);
    }
    finishTone["pointCurveSetVersion"] = 2;
    finishTone["pointCurves"] = std::move(curveSet);
    finishTone["luminanceTone"] = SerializeSceneTone(ReadSceneTone(
        finishTone.value("luminanceTone", nlohmann::json::object())));
    return finishTone;
}

RawPointCurveSet PointCurveSetFromFinishToneJson(const nlohmann::json& finishTone) {
    const nlohmann::json sanitized =
        SanitizeFinishTonePointCurveJson(finishTone);
    RawPointCurveSet result;
    result.version = 2;
    const nlohmann::json& pointCurves = sanitized["pointCurves"];
    for (int index = 0; index < 4; ++index) {
        const char* key = RawPointCurveChannelKey(static_cast<RawPointCurveChannel>(index));
        result.curves[static_cast<std::size_t>(index)] =
            SanitizePointCurveComponent(pointCurves[key]);
    }
    return result;
}

void StorePointCurveSetInFinishToneJson(
    nlohmann::json& finishTone,
    const RawPointCurveSet& curveSet) {
    finishTone = SanitizeFinishTonePointCurveJson(finishTone);
    finishTone["pointCurveSetVersion"] = 2;
    for (int index = 0; index < 4; ++index) {
        const RawPointCurveComponent& component =
            curveSet.curves[static_cast<std::size_t>(index)];
        finishTone["pointCurves"][RawPointCurveChannelKey(
            static_cast<RawPointCurveChannel>(index))] =
            SerializePointCurveComponent(SanitizePointCurveComponent(
                SerializePointCurveComponent(component)));
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
    finishTone = SanitizeFinishTonePointCurveJson(std::move(finishTone));
    finishTone["pointCurves"][RawPointCurveChannelKey(channel)] =
        SerializePointCurveComponent(SanitizePointCurveComponent(
            SerializePointCurveComponent(component)));
}

namespace {

float BezierCoordinate(float p0, float p1, float p2, float p3, float t) {
    const float inverse = 1.0f - t;
    return inverse * inverse * inverse * p0 +
        3.0f * inverse * inverse * t * p1 +
        3.0f * inverse * t * t * p2 +
        t * t * t * p3;
}

std::array<float, 2> NormalizeCurveVector(float x, float y) {
    const float length = std::sqrt(x * x + y * y);
    if (length <= 0.000001f) {
        return { 1.0f, 0.0f };
    }
    return { x / length, y / length };
}

std::vector<float> MonotoneCurveTangents(
    const std::vector<RawBezierCurvePoint>& points) {
    std::vector<float> tangents(points.size(), 0.0f);
    if (points.size() < 2u) {
        return tangents;
    }
    std::vector<float> widths(points.size() - 1u, 0.0f);
    std::vector<float> slopes(points.size() - 1u, 0.0f);
    for (std::size_t index = 0; index + 1u < points.size(); ++index) {
        widths[index] = std::max(0.0001f, points[index + 1u].x - points[index].x);
        slopes[index] = (points[index + 1u].y - points[index].y) / widths[index];
    }
    tangents.front() = slopes.front();
    tangents.back() = slopes.back();
    for (std::size_t index = 1; index + 1u < points.size(); ++index) {
        if (slopes[index - 1u] * slopes[index] <= 0.0f) {
            tangents[index] = 0.0f;
            continue;
        }
        const float w1 = 2.0f * widths[index] + widths[index - 1u];
        const float w2 = widths[index] + 2.0f * widths[index - 1u];
        tangents[index] = (w1 + w2) /
            (w1 / slopes[index - 1u] + w2 / slopes[index]);
    }
    return tangents;
}

std::array<float, 2> RotateCurveHandleToward(
    float baseX,
    float baseY,
    float targetX,
    float targetY,
    float amount) {
    const float length = std::sqrt(baseX * baseX + baseY * baseY);
    if (length <= 0.000001f) {
        return { 0.0f, 0.0f };
    }
    const std::array<float, 2> baseDirection =
        NormalizeCurveVector(baseX, baseY);
    const std::array<float, 2> targetDirection =
        NormalizeCurveVector(targetX, targetY);
    std::array<float, 2> direction = NormalizeCurveVector(
        baseDirection[0] + (targetDirection[0] - baseDirection[0]) * amount,
        baseDirection[1] + (targetDirection[1] - baseDirection[1]) * amount);
    return { direction[0] * length, direction[1] * length };
}

std::array<float, 2> ClampCurveHandleFromAnchor(
    float anchorX,
    float anchorY,
    float handleX,
    float handleY,
    float minimumX,
    float maximumX) {
    const float dx = handleX - anchorX;
    const float dy = handleY - anchorY;
    float scale = 1.0f;
    if (dx > 0.000001f) {
        scale = std::min(scale, (maximumX - anchorX) / dx);
    } else if (dx < -0.000001f) {
        scale = std::min(scale, (minimumX - anchorX) / dx);
    }
    if (dy > 0.000001f) {
        scale = std::min(scale, (1.0f - anchorY) / dy);
    } else if (dy < -0.000001f) {
        scale = std::min(scale, (0.0f - anchorY) / dy);
    }
    scale = std::clamp(scale, 0.0f, 1.0f);
    return {
        anchorX + dx * scale,
        anchorY + dy * scale
    };
}

} // namespace

RawBezierSegment BuildRawBezierSegment(
    const std::vector<RawBezierCurvePoint>& inputPoints,
    std::size_t segmentIndex) {
    RawBezierSegment result;
    if (inputPoints.size() < 2u) {
        return result;
    }
    const std::size_t count = std::min(inputPoints.size(), kMaxRawPointCurvePoints);
    segmentIndex = std::min(segmentIndex, count - 2u);
    std::vector<RawBezierCurvePoint> points(inputPoints.begin(), inputPoints.begin() + count);
    for (RawBezierCurvePoint& point : points) {
        point.x = std::clamp(point.x, 0.0f, 1.0f);
        point.y = std::clamp(point.y, 0.0f, 1.0f);
        point.incoming = SanitizeBezierHandle(point.incoming);
        point.outgoing = SanitizeBezierHandle(point.outgoing);
    }
    result.left = points[segmentIndex];
    result.right = points[segmentIndex + 1u];
    const float width = std::max(0.0001f, result.right.x - result.left.x);
    const float straightLeftX = result.left.x + width / 3.0f;
    const float straightLeftY = result.left.y + (result.right.y - result.left.y) / 3.0f;
    const float straightRightX = result.right.x - width / 3.0f;
    const float straightRightY = result.right.y - (result.right.y - result.left.y) / 3.0f;
    const std::vector<float> tangents = MonotoneCurveTangents(points);

    const RawBezierHandleState& leftState = result.left.outgoing;
    float baseLeftX = leftState.manual
        ? result.left.x + leftState.offsetX
        : result.left.x + width / 3.0f;
    float baseLeftY = leftState.manual
        ? result.left.y + leftState.offsetY
        : result.left.y + tangents[segmentIndex] * width / 3.0f;
    const float leftBlend = std::min(leftState.strength, 1.0f);
    result.leftHandleX = straightLeftX + (baseLeftX - straightLeftX) * leftBlend;
    result.leftHandleY = straightLeftY + (baseLeftY - straightLeftY) * leftBlend;
    if (leftState.strength > 1.0f) {
        const RawBezierCurvePoint& targetPoint = segmentIndex > 0u
            ? points[segmentIndex - 1u]
            : result.right;
        const float targetX = segmentIndex > 0u
            ? result.left.x - targetPoint.x
            : result.right.x - result.left.x;
        const float targetY = segmentIndex > 0u
            ? result.left.y - targetPoint.y
            : result.right.y - result.left.y;
        const auto rotated = RotateCurveHandleToward(
            result.leftHandleX - result.left.x,
            result.leftHandleY - result.left.y,
            targetX,
            targetY,
            (leftState.strength - 1.0f) / 0.5f);
        result.leftHandleX = result.left.x + rotated[0];
        result.leftHandleY = result.left.y + rotated[1];
    }

    const RawBezierHandleState& rightState = result.right.incoming;
    float baseRightX = rightState.manual
        ? result.right.x + rightState.offsetX
        : result.right.x - width / 3.0f;
    float baseRightY = rightState.manual
        ? result.right.y + rightState.offsetY
        : result.right.y - tangents[segmentIndex + 1u] * width / 3.0f;
    const float rightBlend = std::min(rightState.strength, 1.0f);
    result.rightHandleX = straightRightX + (baseRightX - straightRightX) * rightBlend;
    result.rightHandleY = straightRightY + (baseRightY - straightRightY) * rightBlend;
    if (rightState.strength > 1.0f) {
        const RawBezierCurvePoint& targetPoint = segmentIndex + 2u < points.size()
            ? points[segmentIndex + 2u]
            : result.left;
        const float targetX = segmentIndex + 2u < points.size()
            ? result.right.x - targetPoint.x
            : result.left.x - result.right.x;
        const float targetY = segmentIndex + 2u < points.size()
            ? result.right.y - targetPoint.y
            : result.left.y - result.right.y;
        const auto rotated = RotateCurveHandleToward(
            result.rightHandleX - result.right.x,
            result.rightHandleY - result.right.y,
            targetX,
            targetY,
            (rightState.strength - 1.0f) / 0.5f);
        result.rightHandleX = result.right.x + rotated[0];
        result.rightHandleY = result.right.y + rotated[1];
    }

    const auto clampedLeft = ClampCurveHandleFromAnchor(
        result.left.x,
        result.left.y,
        result.leftHandleX,
        result.leftHandleY,
        result.left.x,
        result.right.x);
    result.leftHandleX = clampedLeft[0];
    result.leftHandleY = clampedLeft[1];
    const auto clampedRight = ClampCurveHandleFromAnchor(
        result.right.x,
        result.right.y,
        result.rightHandleX,
        result.rightHandleY,
        result.left.x,
        result.right.x);
    result.rightHandleX = clampedRight[0];
    result.rightHandleY = clampedRight[1];
    if (result.leftHandleX > result.rightHandleX) {
        const float meetingX =
            (result.leftHandleX + result.rightHandleX) * 0.5f;
        const float leftDx = result.leftHandleX - result.left.x;
        const float rightDx = result.rightHandleX - result.right.x;
        if (leftDx > 0.000001f) {
            const float scale = std::clamp(
                (meetingX - result.left.x) / leftDx,
                0.0f,
                1.0f);
            result.leftHandleX = result.left.x + leftDx * scale;
            result.leftHandleY = result.left.y +
                (result.leftHandleY - result.left.y) * scale;
        }
        if (rightDx < -0.000001f) {
            const float scale = std::clamp(
                (meetingX - result.right.x) / rightDx,
                0.0f,
                1.0f);
            result.rightHandleX = result.right.x + rightDx * scale;
            result.rightHandleY = result.right.y +
                (result.rightHandleY - result.right.y) * scale;
        }
    }
    return result;
}

float EvaluateRawBezierCurve(
    const std::vector<RawBezierCurvePoint>& points,
    float x) {
    if (points.empty()) {
        return std::clamp(x, 0.0f, 1.0f);
    }
    if (points.size() == 1u) {
        return std::clamp(points.front().y, 0.0f, 1.0f);
    }
    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= points.front().x) return std::clamp(points.front().y, 0.0f, 1.0f);
    if (x >= points.back().x) return std::clamp(points.back().y, 0.0f, 1.0f);
    std::size_t segmentIndex = 0u;
    while (segmentIndex + 1u < points.size() &&
           x > points[segmentIndex + 1u].x) {
        ++segmentIndex;
    }
    segmentIndex = std::min(segmentIndex, points.size() - 2u);
    const RawBezierSegment segment = BuildRawBezierSegment(points, segmentIndex);
    float low = 0.0f;
    float high = 1.0f;
    for (int iteration = 0; iteration < 18; ++iteration) {
        const float middle = (low + high) * 0.5f;
        const float curveX = BezierCoordinate(
            segment.left.x,
            segment.leftHandleX,
            segment.rightHandleX,
            segment.right.x,
            middle);
        if (curveX < x) low = middle;
        else high = middle;
    }
    const float t = (low + high) * 0.5f;
    return std::clamp(
        BezierCoordinate(
            segment.left.y,
            segment.leftHandleY,
            segment.rightHandleY,
            segment.right.y,
            t),
        0.0f,
        1.0f);
}

std::vector<RawBezierCurvePoint> RawPointCurveBezierPoints(
    const RawPointCurveComponent& component) {
    std::vector<RawBezierCurvePoint> result;
    result.reserve(component.points.size());
    for (const RawPointCurveControlPoint& point : component.points) {
        result.push_back({ point.x, point.y, point.incoming, point.outgoing });
    }
    return result;
}

std::vector<RawBezierCurvePoint> RawLocalRangeBezierPoints(
    const RawLocalRangeRecipe& localRangeInput) {
    const RawLocalRangeRecipe localRange = SanitizeLocalRangeRecipe(localRangeInput);
    const float evSpan = std::max(0.1f, localRange.maxEv - localRange.minEv);
    std::vector<RawBezierCurvePoint> result;
    result.reserve(localRange.points.size());
    for (const RawLocalRangePoint& point : localRange.points) {
        result.push_back({
            (point.ev - localRange.minEv) / evSpan,
            (point.deltaEv + 4.0f) / 8.0f,
            point.incoming,
            point.outgoing
        });
    }
    return result;
}

float EvaluateRawPointCurve(
    const std::vector<RawPointCurveControlPoint>& points,
    const std::string&,
    float x) {
    RawPointCurveComponent component;
    component.points = points;
    return EvaluateRawBezierCurve(RawPointCurveBezierPoints(component), x);
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
    auto identity = [&](const std::vector<RawPointCurveControlPoint>& points) {
        if (points.empty()) return true;
        for (const RawPointCurveControlPoint& point : points) {
            if (std::abs(point.x - point.y) > 0.0001f) return false;
        }
        for (int sample = 0; sample <= 64; ++sample) {
            const float coordinate = static_cast<float>(sample) / 64.0f;
            if (std::abs(EvaluateRawPointCurve(
                    points,
                    component.interpolation,
                    coordinate) - coordinate) > 0.0001f) {
                return false;
            }
        }
        return true;
    };
    return identity(component.basePoints) && identity(component.points);
}

std::array<float, 3> EvaluateFinishTonePointCurveRgb(
    const nlohmann::json& finishTone,
    const std::array<float, 3>& sceneRgb) {
    const Raw::RawWorkingSpace workingSpace =
        finishTone.value("inputWorkingSpace", std::string("linear-srgb-d65")) ==
                "linear-rec2020-d65"
            ? Raw::RawWorkingSpace::LinearRec2020D65
            : Raw::RawWorkingSpace::LinearSrgbD65;
    return EvaluateFinishTonePointCurveRgb(finishTone, sceneRgb, workingSpace);
}

std::array<float, 3> EvaluateFinishTonePointCurveRgb(
    const nlohmann::json& finishTone,
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace) {
    const RawPointCurveSet curveSet = PointCurveSetFromFinishToneJson(finishTone);
    const int domain = std::clamp(finishTone.value("domain", 1), 0, 1);
    const float minimumEv = ClampFinite(
        finishTone.value("logMinEv", -10.0f), -10.0f, -20.0f, 0.0f);
    const float maximumEv = std::max(
        minimumEv + 0.1f,
        ClampFinite(finishTone.value("logMaxEv", 6.0f), 6.0f, 0.0f, 20.0f));
    const float middleGrey = ClampFinite(
        finishTone.value("middleGrey", 0.18f), 0.18f, 0.01f, 1.0f);
    std::array<float, 3> rgb = ApplySceneTone(ReadSceneTone(
        finishTone.value("luminanceTone", nlohmann::json::object())), sceneRgb,
        workingSpace == Raw::RawWorkingSpace::LinearRec2020D65
            ? std::array<float,3>{0.2627002f,0.6779981f,0.0593017f}
            : std::array<float,3>{0.2126729f,0.7151522f,0.0721750f});

    const RawPointCurveComponent& composite =
        curveSet.curves[static_cast<std::size_t>(RawPointCurveChannel::Composite)];
    for (int channel = 0; channel < 3; ++channel) {
        const std::size_t channelIndex = static_cast<std::size_t>(channel);
        if (rgb[channelIndex] <= 0.0f) {
            continue;
        }
        float coordinate = PointCurveCoordinateFromSceneUnclamped(
            rgb[channelIndex], domain, minimumEv, maximumEv, middleGrey);
        const float clampedCoordinate = std::clamp(coordinate, 0.0f, 1.0f);
        float mapped = EvaluateRawPointCurveComponent(
            composite,
            clampedCoordinate);
        mapped = EvaluateRawPointCurveComponent(
            curveSet.curves[static_cast<std::size_t>(channel + 1)],
            mapped);
        if (coordinate < 0.0f) {
            mapped += coordinate;
        } else if (coordinate > 1.0f) {
            mapped += coordinate - 1.0f;
        }
        rgb[channelIndex] = PointCurveSceneFromCoordinateUnclamped(
            mapped, domain, minimumEv, maximumEv, middleGrey);
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
        { "curveRangeMode", kFinishToneRangeExtendedSceneV1 },
        { "inputWorkingSpace", "linear-rec2020-d65" },
        { "pointCurveSetVersion", 2 },
        { "pointCurves", std::move(pointCurves) },
        { "luminanceTone", SerializeSceneTone(SceneTone{}) },
        { "logMinEv", -10.0f },
        { "logMaxEv", 6.0f },
        { "middleGrey", 0.18f }
    };
}

const char* RawColorWarpInterpretationModeStableString(
    RawColorWarpInterpretationMode mode) {
    switch (mode) {
        case RawColorWarpInterpretationMode::DominantFamily: return "dominant-family";
        case RawColorWarpInterpretationMode::ConnectedFamily: return "connected-family";
        case RawColorWarpInterpretationMode::MultipleColors: return "multiple-colors";
        case RawColorWarpInterpretationMode::ColorOnly: return "color-only";
        case RawColorWarpInterpretationMode::ColorAndBrightness: return "color-and-brightness";
        case RawColorWarpInterpretationMode::FullContents: return "full-contents";
        case RawColorWarpInterpretationMode::GuidedFamily:
        default: return "guided-family";
    }
}

RawColorWarpInterpretationMode RawColorWarpInterpretationModeFromStableString(
    const std::string& value) {
    if (value == "dominant-family") return RawColorWarpInterpretationMode::DominantFamily;
    if (value == "connected-family") return RawColorWarpInterpretationMode::ConnectedFamily;
    if (value == "multiple-colors") return RawColorWarpInterpretationMode::MultipleColors;
    if (value == "color-only") return RawColorWarpInterpretationMode::ColorOnly;
    if (value == "color-and-brightness") return RawColorWarpInterpretationMode::ColorAndBrightness;
    if (value == "full-contents") return RawColorWarpInterpretationMode::FullContents;
    return RawColorWarpInterpretationMode::GuidedFamily;
}

const char* RawColorWarpSpatialModeStableString(RawColorWarpSpatialMode mode) {
    switch (mode) {
        case RawColorWarpSpatialMode::Connected: return "connected";
        case RawColorWarpSpatialMode::Cohesive: return "cohesive";
        case RawColorWarpSpatialMode::EdgeAwareReach: return "edge-aware-reach";
        case RawColorWarpSpatialMode::AssistedRegion: return "assisted-region";
        case RawColorWarpSpatialMode::AllMatches:
        default: return "all-matches";
    }
}

RawColorWarpSpatialMode RawColorWarpSpatialModeFromStableString(
    const std::string& value) {
    if (value == "connected") return RawColorWarpSpatialMode::Connected;
    if (value == "cohesive") return RawColorWarpSpatialMode::Cohesive;
    if (value == "edge-aware-reach") return RawColorWarpSpatialMode::EdgeAwareReach;
    if (value == "assisted-region") return RawColorWarpSpatialMode::AssistedRegion;
    return RawColorWarpSpatialMode::AllMatches;
}

const char* RawColorWarpFeatherDirectionStableString(
    RawColorWarpFeatherDirection direction) {
    switch (direction) {
        case RawColorWarpFeatherDirection::Inward: return "inward";
        case RawColorWarpFeatherDirection::Outward: return "outward";
        case RawColorWarpFeatherDirection::Centered:
        default: return "centered";
    }
}

RawColorWarpFeatherDirection RawColorWarpFeatherDirectionFromStableString(
    const std::string& value) {
    if (value == "inward") return RawColorWarpFeatherDirection::Inward;
    if (value == "outward") return RawColorWarpFeatherDirection::Outward;
    return RawColorWarpFeatherDirection::Centered;
}

RawColorWarpRecipe SanitizeColorWarpRecipe(RawColorWarpRecipe colorWarp) {
    colorWarp.version = 2;
    colorWarp.strength = ClampFinite(colorWarp.strength, 1.0f, 0.0f, 2.0f);
    if (colorWarp.pins.size() > kMaxRawColorWarpPins) {
        colorWarp.pins.resize(kMaxRawColorWarpPins);
    }
    if (colorWarp.regions.size() > kMaxRawColorWarpRegions) {
        colorWarp.regions.resize(kMaxRawColorWarpRegions);
    }
    if (colorWarp.linkGroups.size() > kMaxRawColorWarpLinkGroups) {
        colorWarp.linkGroups.resize(kMaxRawColorWarpLinkGroups);
    }

    std::unordered_set<std::string> usedRegionIds;
    for (std::size_t regionIndex = 0; regionIndex < colorWarp.regions.size(); ++regionIndex) {
        RawColorWarpRegion& region = colorWarp.regions[regionIndex];
        if (region.id.empty() || usedRegionIds.find(region.id) != usedRegionIds.end()) {
            region.id = "region-" + std::to_string(regionIndex + 1u);
            while (usedRegionIds.find(region.id) != usedRegionIds.end()) {
                region.id += "-copy";
            }
        }
        usedRegionIds.insert(region.id);
        if (region.circles.size() > kMaxRawColorWarpSampleCircles) {
            region.circles.resize(kMaxRawColorWarpSampleCircles);
        }
        std::unordered_set<std::string> usedCircleIds;
        for (std::size_t circleIndex = 0; circleIndex < region.circles.size(); ++circleIndex) {
            RawColorWarpSampleCircle& circle = region.circles[circleIndex];
            if (circle.id.empty() || usedCircleIds.find(circle.id) != usedCircleIds.end()) {
                circle.id = "sample-" + std::to_string(circleIndex + 1u);
                while (usedCircleIds.find(circle.id) != usedCircleIds.end()) {
                    circle.id += "-copy";
                }
            }
            usedCircleIds.insert(circle.id);
            circle.centerU = ClampFinite(circle.centerU, 0.5f, 0.0f, 1.0f);
            circle.centerV = ClampFinite(circle.centerV, 0.5f, 0.0f, 1.0f);
            circle.radiusU = ClampFinite(circle.radiusU, 0.02f, 0.000001f, 1.0f);
            circle.radiusV = ClampFinite(circle.radiusV, 0.02f, 0.000001f, 1.0f);
        }
        region.reachPixels = ClampFinite(region.reachPixels, 24.0f, 0.0f, 4096.0f);
        region.spatialSupport = ClampFinite(region.spatialSupport, 1.0f, 0.0f, 1.0f);
        region.edgeStop = ClampFinite(region.edgeStop, 0.65f, 0.0f, 1.0f);
        region.featherPixels = ClampFinite(region.featherPixels, 0.0f, 0.0f, 4096.0f);
    }

    std::unordered_set<std::string> usedIds;
    for (std::size_t index = 0; index < colorWarp.pins.size(); ++index) {
        RawColorWarpPin& pin = colorWarp.pins[index];
        if (pin.id.empty() || usedIds.find(pin.id) != usedIds.end()) {
            pin.id = "color-" + std::to_string(index + 1u);
            while (usedIds.find(pin.id) != usedIds.end()) {
                pin.id += "-copy";
            }
        }
        usedIds.insert(pin.id);
        if (pin.name.empty()) {
            pin.name = pin.protectColor
                ? "Protected Color " + std::to_string(index + 1u)
                : "Color " + std::to_string(index + 1u);
        }
        pin.sourceA = ClampFinite(pin.sourceA, 0.0f, -1.0f, 1.0f);
        pin.sourceB = ClampFinite(pin.sourceB, 0.0f, -1.0f, 1.0f);
        pin.targetA = ClampFinite(pin.targetA, pin.sourceA, -1.0f, 1.0f);
        pin.targetB = ClampFinite(pin.targetB, pin.sourceB, -1.0f, 1.0f);
        pin.radius = ClampFinite(pin.radius, 0.12f, 0.005f, 1.0f);
        pin.softness = ClampFinite(pin.softness, 0.55f, 0.0f, 1.0f);
        pin.qualifierDirectionality = ClampFinite(
            pin.qualifierDirectionality, 0.0f, 0.0f, 1.0f);
        pin.qualifierOrientationRadians = ClampFinite(
            pin.qualifierOrientationRadians,
            0.0f,
            -3.14159265358979323846f,
            3.14159265358979323846f);
        pin.qualifierAperture = ClampFinite(
            pin.qualifierAperture, 0.45f, 0.10f, 1.0f);
        pin.strength = ClampFinite(pin.strength, 1.0f, 0.0f, 2.0f);
        if (usedRegionIds.find(pin.regionId) == usedRegionIds.end()) {
            pin.regionId.clear();
        }
        if (pin.evCurve.samples.empty()) {
            pin.evCurve = MakeUniformColorWarpEvCurve();
        } else if (pin.evCurve.samples.size() !=
                   kRawColorWarpEvCurveSampleCount) {
            const RawColorWarpEvCurve original = pin.evCurve;
            pin.evCurve = MakeUniformColorWarpEvCurve();
            for (std::size_t sampleIndex = 0;
                 sampleIndex < pin.evCurve.samples.size();
                 ++sampleIndex) {
                const float t = static_cast<float>(sampleIndex) /
                    static_cast<float>(pin.evCurve.samples.size() - 1u);
                const float ev = kRawColorWarpEvMinimum +
                    t * (kRawColorWarpEvMaximum - kRawColorWarpEvMinimum);
                pin.evCurve.samples[sampleIndex] =
                    EvaluateColorWarpEvCurve(original, ev);
            }
        }
        for (float& sample : pin.evCurve.samples) {
            sample = ClampFinite(sample, 1.0f, 0.0f, 1.0f);
        }
        pin.lightnessDeltaEv = ClampFinite(
            pin.lightnessDeltaEv, 0.0f, -4.0f, 4.0f);
    }

    std::unordered_set<std::string> usedGroupIds;
    for (std::size_t groupIndex = 0; groupIndex < colorWarp.linkGroups.size(); ++groupIndex) {
        RawColorWarpLinkGroup& group = colorWarp.linkGroups[groupIndex];
        if (group.id.empty() || usedGroupIds.find(group.id) != usedGroupIds.end()) {
            group.id = "group-" + std::to_string(groupIndex + 1u);
            while (usedGroupIds.find(group.id) != usedGroupIds.end()) {
                group.id += "-copy";
            }
        }
        usedGroupIds.insert(group.id);
        if (group.name.empty()) {
            group.name = "Color Group " + std::to_string(groupIndex + 1u);
        }
        std::unordered_set<std::string> groupPinIds;
        group.pinIds.erase(
            std::remove_if(
                group.pinIds.begin(),
                group.pinIds.end(),
                [&](const std::string& pinId) {
                    return usedIds.find(pinId) == usedIds.end() ||
                        !groupPinIds.insert(pinId).second;
                }),
            group.pinIds.end());
    }
    colorWarp.linkGroups.erase(
        std::remove_if(
            colorWarp.linkGroups.begin(),
            colorWarp.linkGroups.end(),
            [](const RawColorWarpLinkGroup& group) { return group.pinIds.empty(); }),
        colorWarp.linkGroups.end());
    return colorWarp;
}

nlohmann::json SerializeColorWarpRecipe(const RawColorWarpRecipe& input) {
    const RawColorWarpRecipe colorWarp = SanitizeColorWarpRecipe(input);
    nlohmann::json result = nlohmann::json::object();
    result["version"] = colorWarp.version;
    result["enabled"] = colorWarp.enabled;
    result["strength"] = colorWarp.strength;
    result["coordinateModel"] = "oklab-ab-d65-v2";
    result["regions"] = nlohmann::json::array();
    for (const RawColorWarpRegion& region : colorWarp.regions) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = region.id;
        item["spatialMode"] = RawColorWarpSpatialModeStableString(region.spatialMode);
        item["reachPixels"] = region.reachPixels;
        item["spatialSupport"] = region.spatialSupport;
        item["edgeStop"] = region.edgeStop;
        item["featherPixels"] = region.featherPixels;
        item["featherDirection"] =
            RawColorWarpFeatherDirectionStableString(region.featherDirection);
        item["circles"] = nlohmann::json::array();
        for (const RawColorWarpSampleCircle& circle : region.circles) {
            nlohmann::json circleItem = nlohmann::json::object();
            circleItem["id"] = circle.id;
            circleItem["centerU"] = circle.centerU;
            circleItem["centerV"] = circle.centerV;
            circleItem["radiusU"] = circle.radiusU;
            circleItem["radiusV"] = circle.radiusV;
            circleItem["interpretationMode"] =
                RawColorWarpInterpretationModeStableString(circle.interpretation);
            circleItem["polarity"] = circle.polarity == RawColorWarpSamplePolarity::Exclude
                ? "exclude"
                : "include";
            item["circles"].push_back(std::move(circleItem));
        }
        result["regions"].push_back(std::move(item));
    }
    result["pins"] = nlohmann::json::array();
    for (const RawColorWarpPin& pin : colorWarp.pins) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = pin.id;
        item["name"] = pin.name;
        item["enabled"] = pin.enabled;
        item["protectColor"] = pin.protectColor;
        item["sourceA"] = pin.sourceA;
        item["sourceB"] = pin.sourceB;
        item["targetA"] = pin.targetA;
        item["targetB"] = pin.targetB;
        item["radius"] = pin.radius;
        item["softness"] = pin.softness;
        item["qualifierDirectionality"] = pin.qualifierDirectionality;
        item["qualifierOrientationRadians"] = pin.qualifierOrientationRadians;
        item["qualifierAperture"] = pin.qualifierAperture;
        item["strength"] = pin.strength;
        item["regionId"] = pin.regionId;
        item["evCurve"] = {
            { "minimumEv", kRawColorWarpEvMinimum },
            { "maximumEv", kRawColorWarpEvMaximum },
            { "samples", pin.evCurve.samples }
        };
        item["lightnessDeltaEv"] = pin.lightnessDeltaEv;
        result["pins"].push_back(std::move(item));
    }
    result["linkGroups"] = nlohmann::json::array();
    for (const RawColorWarpLinkGroup& group : colorWarp.linkGroups) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = group.id;
        item["name"] = group.name;
        item["pinIds"] = group.pinIds;
        result["linkGroups"].push_back(std::move(item));
    }
    return result;
}

RawColorWarpRecipe DeserializeColorWarpRecipe(const nlohmann::json& value) {
    RawColorWarpRecipe result;
    if (!value.is_object()) {
        return result;
    }
    if (value.value("version", 0) != 2 ||
        value.value("coordinateModel", std::string()) !=
            "oklab-ab-d65-v2") {
        return result;
    }
    result.version = 2;
    result.enabled = value.value("enabled", true);
    result.strength = JsonFloat(value, "strength", 1.0f);
    const nlohmann::json regions = value.value("regions", nlohmann::json::array());
    if (regions.is_array()) {
        for (const nlohmann::json& item : regions) {
            if (!item.is_object()) continue;
            RawColorWarpRegion region;
            region.id = item.value("id", std::string());
            region.spatialMode = RawColorWarpSpatialModeFromStableString(
                item.value("spatialMode", std::string("cohesive")));
            region.reachPixels = JsonFloat(item, "reachPixels", 24.0f);
            region.spatialSupport = JsonFloat(item, "spatialSupport", 1.0f);
            region.edgeStop = JsonFloat(item, "edgeStop", 0.65f);
            region.featherPixels = JsonFloat(item, "featherPixels", 0.0f);
            region.featherDirection = RawColorWarpFeatherDirectionFromStableString(
                item.value("featherDirection", std::string("centered")));
            const nlohmann::json circles = item.value("circles", nlohmann::json::array());
            if (circles.is_array()) {
                for (const nlohmann::json& circleItem : circles) {
                    if (!circleItem.is_object()) continue;
                    RawColorWarpSampleCircle circle;
                    circle.id = circleItem.value("id", std::string());
                    circle.centerU = JsonFloat(circleItem, "centerU", 0.5f);
                    circle.centerV = JsonFloat(circleItem, "centerV", 0.5f);
                    circle.radiusU = JsonFloat(circleItem, "radiusU", 0.02f);
                    circle.radiusV = JsonFloat(circleItem, "radiusV", 0.02f);
                    circle.interpretation = RawColorWarpInterpretationModeFromStableString(
                        circleItem.value(
                            "interpretationMode",
                            std::string("guided-family")));
                    circle.polarity = circleItem.value(
                        "polarity", std::string("include")) == "exclude"
                        ? RawColorWarpSamplePolarity::Exclude
                        : RawColorWarpSamplePolarity::Include;
                    region.circles.push_back(std::move(circle));
                }
            }
            result.regions.push_back(std::move(region));
        }
    }
    const nlohmann::json pins = value.value("pins", nlohmann::json::array());
    if (pins.is_array()) {
        for (const nlohmann::json& item : pins) {
            if (!item.is_object()) {
                continue;
            }
            RawColorWarpPin pin;
            pin.id = item.value("id", std::string());
            pin.name = item.value("name", std::string());
            pin.enabled = item.value("enabled", true);
            pin.protectColor = item.value("protectColor", false);
            pin.sourceA = JsonFloat(item, "sourceA", 0.0f);
            pin.sourceB = JsonFloat(item, "sourceB", 0.0f);
            pin.targetA = JsonFloat(item, "targetA", pin.sourceA);
            pin.targetB = JsonFloat(item, "targetB", pin.sourceB);
            pin.radius = JsonFloat(item, "radius", 0.12f);
            pin.softness = JsonFloat(item, "softness", 0.55f);
            pin.qualifierDirectionality =
                JsonFloat(item, "qualifierDirectionality", 0.0f);
            pin.qualifierOrientationRadians =
                JsonFloat(item, "qualifierOrientationRadians", 0.0f);
            pin.qualifierAperture =
                JsonFloat(item, "qualifierAperture", 0.45f);
            pin.strength = JsonFloat(item, "strength", 1.0f);
            pin.regionId = item.value("regionId", std::string());
            const nlohmann::json evCurve = item.value(
                "evCurve", nlohmann::json::object());
            if (evCurve.is_object()) {
                const nlohmann::json samples = evCurve.value(
                    "samples", nlohmann::json::array());
                if (samples.is_array()) {
                    pin.evCurve.samples.reserve(samples.size());
                    for (const nlohmann::json& sample : samples) {
                        if (sample.is_number()) {
                            pin.evCurve.samples.push_back(sample.get<float>());
                        }
                    }
                }
            }
            pin.lightnessDeltaEv = JsonFloat(item, "lightnessDeltaEv", 0.0f);
            result.pins.push_back(std::move(pin));
        }
    }
    const nlohmann::json linkGroups = value.value("linkGroups", nlohmann::json::array());
    if (linkGroups.is_array()) {
        for (const nlohmann::json& item : linkGroups) {
            if (!item.is_object()) continue;
            RawColorWarpLinkGroup group;
            group.id = item.value("id", std::string());
            group.name = item.value("name", std::string());
            const nlohmann::json pinIds = item.value("pinIds", nlohmann::json::array());
            if (pinIds.is_array()) {
                for (const nlohmann::json& pinId : pinIds) {
                    if (pinId.is_string()) group.pinIds.push_back(pinId.get<std::string>());
                }
            }
            result.linkGroups.push_back(std::move(group));
        }
    }
    return SanitizeColorWarpRecipe(std::move(result));
}

bool IsColorWarpEnabled(const RawColorWarpRecipe& input) {
    const RawColorWarpRecipe colorWarp = SanitizeColorWarpRecipe(input);
    return HasPreparedColorWarpEffect(colorWarp);
}

float EvaluateColorWarpPinShapeDistance(
    const RawColorWarpPin& pin,
    float a,
    float b) {
    const float da = a - pin.sourceA;
    const float db = b - pin.sourceB;
    const float radius = std::max(0.005f, pin.radius);
    const float radialDistance = std::sqrt(da * da + db * db);
    const float circularDistance = radialDistance / radius;
    const float directionality = std::clamp(
        pin.qualifierDirectionality,
        0.0f,
        1.0f);
    if (directionality <= 0.000001f || radialDistance <= 0.000001f) {
        return circularDistance;
    }
    const float orientation = pin.qualifierOrientationRadians;
    const float directionA = std::cos(orientation);
    const float directionB = std::sin(orientation);
    const float cosine = std::clamp(
        (da * directionA + db * directionB) / radialDistance,
        -1.0f,
        1.0f);
    const float forward = (cosine + 1.0f) * 0.5f;
    const float aperture = std::clamp(pin.qualifierAperture, 0.10f, 1.0f);
    const float lobePower = 1.0f + 4.0f * (1.0f - aperture);
    const float directionalReach = std::max(
        0.05f,
        aperture + (1.0f - aperture) * std::pow(forward, lobePower));
    const float directionalDistance = circularDistance / directionalReach;
    return circularDistance +
        (directionalDistance - circularDistance) * directionality;
}

float EvaluateColorWarpPinShapeWeight(
    const RawColorWarpPin& pin,
    float a,
    float b) {
    const float distance = EvaluateColorWarpPinShapeDistance(pin, a, b);
    if (distance >= 1.0f) {
        return 0.0f;
    }
    const float innerBoundary = std::max(
        0.0f,
        1.0f - std::clamp(pin.softness, 0.0f, 1.0f));
    if (distance <= innerBoundary) {
        return 1.0f;
    }
    return 1.0f - SmoothStep(innerBoundary, 1.0f, distance);
}

float EvaluateColorWarpPinLightnessWeight(
    const RawColorWarpPin& pin,
    float sceneEv) {
    return EvaluateColorWarpEvCurve(pin.evCurve, sceneEv);
}

RawColorWarpCoordinate WorkingRgbToColorWarpCoordinate(
    const std::array<float, 3>& rgb,
    Raw::RawWorkingSpace workingSpace) {
    const bool rec2020 =
        workingSpace == Raw::RawWorkingSpace::LinearRec2020D65;
    const float x = rec2020
        ? 0.6369580f * rgb[0] + 0.1446169f * rgb[1] + 0.1688809f * rgb[2]
        : 0.4124564f * rgb[0] + 0.3575761f * rgb[1] + 0.1804375f * rgb[2];
    const float y = rec2020
        ? 0.2627002f * rgb[0] + 0.6779981f * rgb[1] + 0.0593017f * rgb[2]
        : 0.2126729f * rgb[0] + 0.7151522f * rgb[1] + 0.0721750f * rgb[2];
    const float z = rec2020
        ? 0.0280727f * rgb[1] + 1.0609851f * rgb[2]
        : 0.0193339f * rgb[0] + 0.1191920f * rgb[1] + 0.9503041f * rgb[2];
    const float l = std::cbrt(
        0.8190224380f * x + 0.3619062601f * y - 0.1288737815f * z);
    const float m = std::cbrt(
        0.0329836539f * x + 0.9292868616f * y + 0.0361446664f * z);
    const float s = std::cbrt(
        0.0481771894f * x + 0.2642395318f * y + 0.6335478285f * z);
    RawColorWarpCoordinate result;
    result.lightness = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    result.a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    result.b = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
    result.sceneEv = std::log2(std::max(y, 0.000001f) / 0.18f);
    return result;
}

std::array<float, 3> ColorWarpCoordinateToWorkingRgb(
    const RawColorWarpCoordinate& coordinate,
    Raw::RawWorkingSpace workingSpace) {
    const float a = coordinate.a;
    const float b = coordinate.b;
    const float lRoot = coordinate.lightness + 0.3963377774f * a + 0.2158037573f * b;
    const float mRoot = coordinate.lightness - 0.1055613458f * a - 0.0638541728f * b;
    const float sRoot = coordinate.lightness - 0.0894841775f * a - 1.2914855480f * b;
    const float l = lRoot * lRoot * lRoot;
    const float m = mRoot * mRoot * mRoot;
    const float s = sRoot * sRoot * sRoot;
    const float x = 1.2268798734f * l - 0.5578149966f * m + 0.2813910502f * s;
    const float y = -0.0405757626f * l + 1.1122868294f * m - 0.0717110667f * s;
    const float z = -0.0763729497f * l - 0.4214933240f * m + 1.5869240244f * s;
    if (workingSpace == Raw::RawWorkingSpace::LinearRec2020D65) {
        return {
            1.7166512f * x - 0.3556708f * y - 0.2533663f * z,
            -0.6666844f * x + 1.6164812f * y + 0.0157685f * z,
            0.0176399f * x - 0.0427706f * y + 0.9421031f * z
        };
    }
    return {
        3.2404542f * x - 1.5371385f * y - 0.4985314f * z,
        -0.9692660f * x + 1.8760108f * y + 0.0415560f * z,
        0.0556434f * x - 0.2040259f * y + 1.0572252f * z
    };
}

std::array<float, 3> ApplyColorWarp(
    const RawColorWarpRecipe& input,
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace) {
    const RawColorWarpRecipe colorWarp = SanitizeColorWarpRecipe(input);
    return ApplyPreparedColorWarp(colorWarp, sceneRgb, workingSpace);
}

std::array<float, 3> ApplyPreparedColorWarp(
    const RawColorWarpRecipe& colorWarp,
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace) {
    if (!HasPreparedColorWarpEffect(colorWarp)) {
        return sceneRgb;
    }
    RawColorWarpCoordinate coordinate =
        WorkingRgbToColorWarpCoordinate(
            sceneRgb,
            workingSpace);
    float weightedA = 0.0f;
    float weightedB = 0.0f;
    float weightedLightnessEv = 0.0f;
    float totalWeight = 0.0f;
    for (const RawColorWarpPin& pin : colorWarp.pins) {
        if (!pin.enabled || pin.strength <= 0.0001f) {
            continue;
        }
        const float radialWeight = EvaluateColorWarpPinShapeWeight(
            pin,
            coordinate.a,
            coordinate.b);
        if (radialWeight <= 0.000001f) {
            continue;
        }
        const float lightnessWeight = EvaluateColorWarpPinLightnessWeight(
            pin,
            coordinate.sceneEv);
        const float weight = radialWeight * lightnessWeight * pin.strength;
        if (weight <= 0.000001f) {
            continue;
        }
        totalWeight += weight;
        if (!pin.protectColor) {
            weightedA += (pin.targetA - pin.sourceA) * weight;
            weightedB += (pin.targetB - pin.sourceB) * weight;
            weightedLightnessEv += pin.lightnessDeltaEv * weight;
        }
    }
    if (totalWeight <= 0.000001f) {
        return sceneRgb;
    }
    const float denominator = std::max(1.0f, totalWeight);
    coordinate.a += colorWarp.strength * weightedA / denominator;
    coordinate.b += colorWarp.strength * weightedB / denominator;
    std::array<float, 3> output =
        ColorWarpCoordinateToWorkingRgb(
            coordinate,
            workingSpace);
    const float lightnessGain = std::exp2(
        colorWarp.strength * weightedLightnessEv / denominator);
    for (float& channel : output) {
        channel *= lightnessGain;
    }
    return output;
}

nlohmann::json DefaultViewTransformJson() {
    return {
        { "type", "ViewTransform" },
        { "displayCurve", "standard" },
        { "enabled", true },
        { "exposure", 0.0f },
        { "blackEv", -8.0f },
        { "whiteEv", 4.0f },
        { "middleGrey", 0.18f },
        { "shoulder", 0.45f },
        { "toe", 0.18f },
        { "contrast", 1.0f },
        { "contrastPivotEv", 0.0f },
        { "contrastModel", kViewContrastModelPivotedLogV2 },
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
    float contrast,
    float contrastPivotEv) {
    // Keep this scalar reference in lockstep with filmicCurve() in
    // ToneLayerRendering.cpp. RAW Lab uses it only to visualize the existing
    // display transform; it does not define or replace the render path.
    const float black = middleGrey * std::exp2(blackEv);
    const float white = middleGrey * std::exp2(whiteEv);
    float x = std::max(0.0f, input * std::exp2(exposure) - black);
    float normalized = x / std::max(0.000001f, white - black);
    const float safeContrast = std::max(0.05f, contrast);
    const float pivotInput = middleGrey * std::exp2(contrastPivotEv);
    const float pivotNormalized = std::max(
        0.000001f,
        (pivotInput - black) / std::max(0.000001f, white - black));
    normalized = pivotNormalized * std::pow(
        std::max(0.0f, normalized) / pivotNormalized,
        safeContrast);
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
    SanitizeZoneAreas(localRange.areas);
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
            std::clamp(point.deltaEv, -4.0f, 4.0f),
            SanitizeBezierHandle(point.incoming),
            SanitizeBezierHandle(point.outgoing)
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
    rgbDenoise.edgeSensitivity =
        ClampFinite(rgbDenoise.edgeSensitivity, defaults.edgeSensitivity, 0.0f, 1.0f);
    rgbDenoise.maximumStructureSize = ClampFinite(
        rgbDenoise.maximumStructureSize,
        defaults.maximumStructureSize,
        8.0f,
        2048.0f);
    rgbDenoise.lumaMap = SanitizeRawDenoiseControlMap(
        std::move(rgbDenoise.lumaMap),
        0.0f);
    rgbDenoise.chromaMap = SanitizeRawDenoiseControlMap(
        std::move(rgbDenoise.chromaMap),
        0.0f);
    if (rgbDenoise.diagnosticMode < RawDenoiseDiagnosticMode::None ||
        rgbDenoise.diagnosticMode > RawDenoiseDiagnosticMode::EstimatedNoise) {
        rgbDenoise.diagnosticMode = RawDenoiseDiagnosticMode::None;
    }
    if (rgbDenoise.diagnosticLayer != RawDenoiseMapLayer::Luma &&
        rgbDenoise.diagnosticLayer != RawDenoiseMapLayer::Chroma) {
        rgbDenoise.diagnosticLayer = RawDenoiseMapLayer::Luma;
    }
    std::uint64_t highestPointId = 0;
    for (const RawDenoiseControlPoint& point : rgbDenoise.lumaMap.points) {
        highestPointId = std::max(highestPointId, point.id);
    }
    for (const RawDenoiseControlPoint& point : rgbDenoise.chromaMap.points) {
        highestPointId = std::max(highestPointId, point.id);
    }
    rgbDenoise.nextControlPointId = std::max(
        std::max<std::uint64_t>(1, rgbDenoise.nextControlPointId),
        highestPointId + 1);
    return rgbDenoise;
}

bool HasRgbDenoiseEffect(const RawRgbDenoiseRecipe& input) {
    const RawRgbDenoiseRecipe rgbDenoise =
        SanitizeRgbDenoiseRecipe(input);
    if (rgbDenoise.method == RawRgbDenoiseMethod::ClassicalMultiscaleV1) {
        return HasRawDenoiseControlMapEffect(rgbDenoise.lumaMap) ||
            HasRawDenoiseControlMapEffect(rgbDenoise.chromaMap);
    }
    return rgbDenoise.colorNoise > 0.000001f ||
        rgbDenoise.luminanceNoise > 0.000001f;
}

bool IsRgbDenoiseActive(const RawRgbDenoiseRecipe& input) {
    return input.enabled && HasRgbDenoiseEffect(input);
}

RawLocalRangeRecipe ApplyLocalRangePreset(RawLocalRangeRecipe localRange, RawLocalRangePreset preset) {
    if (preset == RawLocalRangePreset::Reset) {
        const RawLocalRangeRecipe sanitized = SanitizeLocalRangeRecipe(std::move(localRange));
        RawLocalRangeRecipe reset = DefaultLocalRangeRecipe();
        reset.targetZoneCombineMode = sanitized.targetZoneCombineMode;
        reset.targetZones = sanitized.targetZones;
        reset.areas = sanitized.areas;
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

namespace {

float EvaluateSanitizedLocalRangeControlDeltaEv(
    const RawLocalRangeRecipe& sanitized,
    float sceneEv) {
    if (sanitized.points.size() < 2 || !std::isfinite(sceneEv)) {
        return 0.0f;
    }

    const float coordinate = std::clamp(
        (sceneEv - sanitized.minEv) /
            std::max(0.1f, sanitized.maxEv - sanitized.minEv),
        0.0f,
        1.0f);
    return -4.0f +
        EvaluateRawBezierCurve(
            RawLocalRangeBezierPoints(sanitized),
            coordinate) * 8.0f;
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
        GradientToneJson(a.toneGradients) == GradientToneJson(b.toneGradients) &&
        SerializeColorWarpRecipe(a.colorWarp) ==
            SerializeColorWarpRecipe(b.colorWarp) &&
        SerializeDetailContrast(a.detailContrast) == SerializeDetailContrast(b.detailContrast) &&
        a.viewTransform.layerJson == b.viewTransform.layerJson;
}

std::size_t FinishStateHash(const RawDevelopmentRecipe& recipe) {
    std::size_t seed = std::hash<std::string>{}(recipe.finishTone.layerJson.dump());
    seed ^= std::hash<std::string>{}(SerializeDetailContrast(recipe.detailContrast).dump()) +
        0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::string>{}(GradientToneJson(recipe.toneGradients).dump()) +
        0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::string>{}(
        SerializeColorWarpRecipe(recipe.colorWarp).dump()) +
        0x9e3779b97f4a7c15ull +
        (seed << 6) +
        (seed >> 2);
    seed ^= std::hash<std::string>{}(recipe.viewTransform.layerJson.dump()) +
        0x9e3779b97f4a7c15ull +
        (seed << 6) +
        (seed >> 2);
    return seed;
}

bool LocalRangeStateEquals(const RawDevelopmentRecipe& a, const RawDevelopmentRecipe& b) {
    return LocalRangeJson(a.localRange) == LocalRangeJson(b.localRange) &&
        GradientEvJson(a.evGradients) == GradientEvJson(b.evGradients);
}

std::size_t LocalRangeStateHash(const RawDevelopmentRecipe& recipe) {
    std::size_t seed = std::hash<std::string>{}(LocalRangeJson(recipe.localRange).dump());
    seed ^= std::hash<std::string>{}(GradientEvJson(recipe.evGradients).dump()) +
        0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

Raw::RawDevelopSettings ToRawDevelopSettings(const RawDevelopmentRecipe& recipe) {
    Raw::RawDevelopSettings settings;
    settings.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    settings.demosaicMethod = recipe.technical.demosaicMethod;
    settings.workingSpace = recipe.technical.workingSpace;
    settings.applyBaselineExposure = recipe.technical.applyBaselineExposure;
    settings.encodeSrgbOutput = recipe.technical.encodeSrgbOutput;
    settings.mosaicDenoise =
        SanitizeMosaicDenoiseSettings(recipe.technical.mosaicDenoise);
    settings.highlightMode = Raw::HighlightReconstructionMode::Off;
    settings.falseColorSuppression = 0.0f;
    settings.defringeStrength = 0.0f;
    settings.highlightEdgeCleanup = 0.0f;
    settings.exposureStops = recipe.preToneExposureEv;
    switch (recipe.whiteBalance.mode) {
        case WhiteBalanceMode::CustomMultipliers:
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

bool IsLocalRangeEnabled(const RawLocalRangeRecipe& localRangeInput) {
    if (HasZoneAreaGain(localRangeInput)) return true;
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
    if (IsLocalRangeEnabled(recipe.localRange)) return true;
    return std::any_of(recipe.evGradients.begin(), recipe.evGradients.end(),
        [](const RawGradientEvAdjustment& adjustment) {
            return adjustment.mask.enabled && IsLocalRangeEnabled(adjustment.curve);
        });
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
    nlohmann::json finishTone = SanitizeFinishToneJson(
        recipe.finishTone.layerJson);
    finishTone["inputWorkingSpace"] =
        WorkingSpaceStableString(recipe.technical.workingSpace);
    const nlohmann::json viewTransform = SanitizeViewTransformJson(
        recipe.viewTransform.layerJson);
    nlohmann::json knownFields = {
        { "rawRecipeVersion", kRawDevelopmentRecipeVersion },
        { "processing", {
            { "version", "truthful-v2" },
            { "demosaic", DemosaicMethodStableString(recipe.technical.demosaicMethod) },
            { "workingSpace", WorkingSpaceStableString(recipe.technical.workingSpace) },
            { "applyBaselineExposure", recipe.technical.applyBaselineExposure },
            { "outputTransfer",
                viewTransform.value(
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
            { "hasMultipliers", recipe.whiteBalance.hasMultipliers },
            { "multipliers", recipe.whiteBalance.multipliers }
        } },
        { "rgbDenoise", {
            { "version", 5 },
            { "enabled", rgbDenoise.enabled },
            { "method", RgbDenoiseMethodStableString(rgbDenoise.method) },
            { "mapping", RgbDenoiseMappingStableString(rgbDenoise.mapping) },
            { "packageId", rgbDenoise.packageId },
            { "packageVersion", rgbDenoise.packageVersion },
            { "modelSha256", rgbDenoise.modelSha256 },
            { "adapterVersion", rgbDenoise.adapterVersion },
            { "colorNoise", rgbDenoise.colorNoise },
            { "luminanceNoise", rgbDenoise.luminanceNoise },
            { "detailProtection", rgbDenoise.detailProtection },
            { "edgeSensitivity", rgbDenoise.edgeSensitivity },
            { "maximumStructureSize", rgbDenoise.maximumStructureSize },
            { "lumaMap", SerializeRawDenoiseControlMap(rgbDenoise.lumaMap) },
            { "chromaMap", SerializeRawDenoiseControlMap(rgbDenoise.chromaMap) },
            { "nextControlPointId", rgbDenoise.nextControlPointId }
        } },
        { "exposureEv", recipe.preToneExposureEv },
        { "colorCalibration", SerializeColorCalibration(recipe.colorCalibration) },
        { "localRange", LocalRangeJson(recipe.localRange) },
        { "evGradients", GradientEvJson(recipe.evGradients) },
        { "finishTone", finishTone },
        { "toneGradients", GradientToneJson(recipe.toneGradients) },
        { "colorWarp", SerializeColorWarpRecipe(recipe.colorWarp) },
        { "detailContrast", SerializeDetailContrast(recipe.detailContrast) },
        { "viewTransform", viewTransform },
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
        { "stageOrder", DefaultStageOrder() }
    };
    return knownFields;
}

RawDevelopmentRecipe DeserializeRecipe(const nlohmann::json& value) {
    RawDevelopmentRecipe recipe = MakeDefaultRecipe({});
    if (!value.is_object()) {
        return recipe;
    }

    const int storedRecipeVersion = value.value("rawRecipeVersion", 0);
    if (storedRecipeVersion != kRawDevelopmentRecipeVersion) {
        return recipe;
    }
    recipe.rawRecipeVersion = kRawDevelopmentRecipeVersion;
    recipe.colorCalibration = DeserializeColorCalibration(
        value.value("colorCalibration", nlohmann::json::object()));
    recipe.technical.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    const nlohmann::json processing = value.value("processing", nlohmann::json::object());
    if (processing.is_object()) {
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
    recipe.technical.mosaicDenoise =
        SanitizeMosaicDenoiseSettings(recipe.technical.mosaicDenoise);

    const nlohmann::json source =
        value.value("sourceRef", nlohmann::json::object());
    if (source.is_object()) {
        recipe.source.sourcePath = source.value("sourcePath", recipe.source.sourcePath);
        recipe.source.relativePathKey = source.value("relativePathKey", recipe.source.relativePathKey);
        recipe.source.fingerprint = source.value("fingerprint", recipe.source.fingerprint);
        recipe.source.fileSizeBytes = JsonUInt64(source, "fileSizeBytes", recipe.source.fileSizeBytes);
        recipe.source.modifiedTimeTicks = JsonInt64(source, "modifiedTimeTicks", recipe.source.modifiedTimeTicks);
        recipe.source.displayName = source.value("displayName", recipe.source.displayName);
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
        recipe.whiteBalance.hasMultipliers = whiteBalance.value("hasMultipliers", recipe.whiteBalance.hasMultipliers);
        const nlohmann::json multipliers = whiteBalance.value("multipliers", nlohmann::json::array());
        if (multipliers.is_array() && multipliers.size() >= 3) {
            recipe.whiteBalance.multipliers[0] = multipliers[0].get<float>();
            recipe.whiteBalance.multipliers[1] = multipliers[1].get<float>();
            recipe.whiteBalance.multipliers[2] = multipliers[2].get<float>();
        }
    }

    const nlohmann::json rgbDenoise =
        value.value("rgbDenoise", nlohmann::json::object());
    if (rgbDenoise.is_object()) {
        const int rgbDenoiseVersion = rgbDenoise.value("version", 0);
        const bool storedDenoiseEnabled =
            rgbDenoise.value("enabled", recipe.rgbDenoise.enabled);
        recipe.rgbDenoise.enabled =
            storedDenoiseEnabled;
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
        recipe.rgbDenoise.edgeSensitivity =
            JsonFloat(rgbDenoise, "edgeSensitivity", recipe.rgbDenoise.edgeSensitivity);
        recipe.rgbDenoise.maximumStructureSize = JsonFloat(
            rgbDenoise,
            "maximumStructureSize",
            recipe.rgbDenoise.maximumStructureSize);
        if (rgbDenoiseVersion >= 3) {
            recipe.rgbDenoise.lumaMap = DeserializeRawDenoiseControlMap(
                rgbDenoise.value("lumaMap", nlohmann::json::object()),
                0.0f);
            recipe.rgbDenoise.chromaMap = DeserializeRawDenoiseControlMap(
                rgbDenoise.value("chromaMap", nlohmann::json::object()),
                0.0f);
            if (rgbDenoiseVersion == 3) {
                recipe.rgbDenoise.lumaMap.baseMultiplier = std::max(
                    0.0f,
                    recipe.rgbDenoise.lumaMap.baseMultiplier - 1.0f);
                recipe.rgbDenoise.chromaMap.baseMultiplier = std::max(
                    0.0f,
                    recipe.rgbDenoise.chromaMap.baseMultiplier - 1.0f);
                if (!storedDenoiseEnabled) {
                    recipe.rgbDenoise.lumaMap = {};
                    recipe.rgbDenoise.chromaMap = {};
                }
            }
        } else if (storedDenoiseEnabled) {
            recipe.rgbDenoise.lumaMap.baseMultiplier =
                recipe.rgbDenoise.luminanceNoise;
            recipe.rgbDenoise.chromaMap.baseMultiplier =
                recipe.rgbDenoise.colorNoise;
        }
        recipe.rgbDenoise.nextControlPointId = JsonUInt64(
            rgbDenoise,
            "nextControlPointId",
            recipe.rgbDenoise.nextControlPointId);
        if (rgbDenoiseVersion < 5) {
            // Earlier versions derived this field from whether any amount was
            // nonzero, so false did not represent a user-authored bypass.
            recipe.rgbDenoise.enabled = true;
        }
    }
    recipe.rgbDenoise = SanitizeRgbDenoiseRecipe(recipe.rgbDenoise);

    recipe.preToneExposureEv =
        JsonFloat(value, "exposureEv", recipe.preToneExposureEv);

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
        recipe.localRange.areasVersion = localRange.value("areasVersion", 1);
        recipe.localRange.areas = DeserializeZoneAreas(localRange.value("areas", nlohmann::json::array()));
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
                RawLocalRangePoint point;
                point.ev = JsonFloat(item, "ev", 0.0f);
                point.deltaEv = JsonFloat(item, "deltaEv", 0.0f);
                point.incoming = BezierHandleFromJson(
                    item.value("incomingHandle", nlohmann::json::object()),
                    0.0f);
                point.outgoing = BezierHandleFromJson(
                    item.value("outgoingHandle", nlohmann::json::object()),
                    0.0f);
                recipe.localRange.points.push_back(point);
            }
        }
    }
    recipe.localRange = SanitizeLocalRangeRecipe(recipe.localRange);

    const nlohmann::json evGradients = value.value("evGradients", nlohmann::json::array());
    if (evGradients.is_array()) {
        for (const auto& item : evGradients) {
            if (!item.is_object() || recipe.evGradients.size() >= kMaxRawGradientAdjustments) break;
            RawGradientEvAdjustment adjustment;
            adjustment.mask = GradientMaskFromJson(item.value("mask", nlohmann::json::object()));
            // Use the same local-curve reader as Background. The small envelope
            // contains no nested gradients, so this has one bounded level.
            adjustment.curve = DeserializeRecipe({
                {"rawRecipeVersion", kRawDevelopmentRecipeVersion},
                {"localRange", item.value("curve", nlohmann::json::object())}}).localRange;
            recipe.evGradients.push_back(std::move(adjustment));
        }
    }

    const nlohmann::json finishTone = value.value("finishTone", nlohmann::json::object());
    recipe.finishTone.layerJson =
        SanitizeFinishToneJson(finishTone);
    recipe.finishTone.layerJson["inputWorkingSpace"] =
        WorkingSpaceStableString(recipe.technical.workingSpace);
    const nlohmann::json toneGradients = value.value("toneGradients", nlohmann::json::array());
    if (toneGradients.is_array()) {
        for (const auto& item : toneGradients) {
            if (!item.is_object() || recipe.toneGradients.size() >= kMaxRawGradientAdjustments) break;
            RawGradientToneAdjustment adjustment;
            adjustment.mask = GradientMaskFromJson(item.value("mask", nlohmann::json::object()));
            adjustment.curveJson = SanitizeFinishToneJson(
                item.value("curve", nlohmann::json::object()));
            recipe.toneGradients.push_back(std::move(adjustment));
        }
    }

    recipe.colorWarp = DeserializeColorWarpRecipe(
        value.value("colorWarp", nlohmann::json::object()));
    recipe.detailContrast = ReadDetailContrast(value.value("detailContrast", nlohmann::json::object()));

    const nlohmann::json viewTransform = value.value("viewTransform", nlohmann::json::object());
    recipe.viewTransform.layerJson = SanitizeViewTransformJson(viewTransform);
    recipe.viewTransform.layerJson["inputWorkingSpace"] =
        WorkingSpaceStableString(recipe.technical.workingSpace);
    recipe.viewTransform.layerJson["encodeSrgbOutput"] =
        recipe.technical.encodeSrgbOutput;

    const nlohmann::json cropRotation =
        value.value("cropRotate", nlohmann::json::object());
    if (cropRotation.is_object()) {
        recipe.cropRotation.cropEnabled = cropRotation.value("cropEnabled", recipe.cropRotation.cropEnabled);
        const nlohmann::json cropRect = cropRotation.value("cropRect", nlohmann::json::object());
        if (cropRect.is_object()) {
            recipe.cropRotation.cropX = JsonFloat(cropRect, "x", recipe.cropRotation.cropX);
            recipe.cropRotation.cropY = JsonFloat(cropRect, "y", recipe.cropRotation.cropY);
            recipe.cropRotation.cropWidth = JsonFloat(cropRect, "width", recipe.cropRotation.cropWidth);
            recipe.cropRotation.cropHeight = JsonFloat(cropRect, "height", recipe.cropRotation.cropHeight);
        }
        recipe.cropRotation.rotationDegrees = cropRotation.value(
            "userRotationDegrees", recipe.cropRotation.rotationDegrees);
        recipe.cropRotation.flipHorizontally = cropRotation.value(
            "flipHorizontally",
            recipe.cropRotation.flipHorizontally);
        recipe.cropRotation.flipVertically = cropRotation.value(
            "flipVertically",
            recipe.cropRotation.flipVertically);
    }

    recipe.stageOrder = DefaultStageOrder();

    return recipe;
}

std::string RecipeDisplayName(const RawDevelopmentRecipe& recipe) {
    if (!recipe.source.displayName.empty()) {
        return recipe.source.displayName;
    }
    return FileNameFromPath(recipe.source.sourcePath);
}

} // namespace Stack::RawRecipe
