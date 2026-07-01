#pragma once

#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Stack::RawRecipe {

inline constexpr int kRawDevelopmentRecipeVersion = 6;

enum class WhiteBalanceMode {
    AsShot,
    Auto,
    CustomMultipliers,
    SampledGrayPoint
};

enum class ToneCurveMode {
    Default,
    Custom
};

enum class RawLocalRangePreset {
    OpenShadows,
    HoldHighlights,
    CompressRange,
    Reset
};

struct RawSourceReference {
    std::string sourcePath;
    std::string relativePathKey;
    std::string fingerprint;
    std::uint64_t fileSizeBytes = 0;
    std::int64_t modifiedTimeTicks = 0;
    std::string displayName;
};

struct RawWhiteBalanceRecipe {
    WhiteBalanceMode mode = WhiteBalanceMode::AsShot;
    bool hasTemperatureKelvin = false;
    float temperatureKelvin = 0.0f;
    bool hasTint = false;
    float tint = 0.0f;
    bool hasMultipliers = false;
    std::array<float, 3> multipliers { 1.0f, 1.0f, 1.0f };
    bool hasSamplePoint = false;
    float sampleX = 0.5f;
    float sampleY = 0.5f;
};

struct RawToneCurvePoint {
    float input = 0.0f;
    float output = 0.0f;
};

struct RawToneCurveRecipe {
    ToneCurveMode mode = ToneCurveMode::Default;
    std::vector<RawToneCurvePoint> points;
};

struct RawLocalExposureRecipe {
    bool enabled = false;
    float amount = 1.0f;
    float shadowLiftEv = 0.0f;
    float highlightCompressionEv = 0.0f;
    float localBaselineEv = 0.0f;
    float noiseGuardBias = 0.0f;
    float highlightGuardBias = 0.0f;
    float shadowGuardBias = 0.0f;
    float smoothGradientProtection = 0.85f;
    float haloGuard = 0.90f;
};

struct RawLocalRangePoint {
    float ev = 0.0f;
    float deltaEv = 0.0f;
};

struct RawLocalRangeRecipe {
    bool enabled = false;
    float strength = 1.0f;
    float middleGrey = 0.18f;
    float minEv = -8.0f;
    float maxEv = 6.0f;
    std::vector<RawLocalRangePoint> points;
    float smoothness = 0.65f;
    float edgeProtection = 0.75f;
    float detailProtection = 0.80f;
    float highlightProtection = 0.50f;
    std::string maskPreviewMode = "none";
    bool regionMaskEnabled = false;
    std::string regionMaskMode = "linear-gradient";
    bool regionMaskInvert = false;
    float regionMaskCenterX = 0.5f;
    float regionMaskCenterY = 0.5f;
    float regionMaskAngleDegrees = 0.0f;
    float regionMaskSize = 0.65f;
    float regionMaskFeather = 0.35f;
    float regionMaskLowEv = -8.0f;
    float regionMaskHighEv = 6.0f;
    bool colorMaskEnabled = false;
    float colorMaskTargetR = 0.0f;
    float colorMaskTargetG = 1.0f;
    float colorMaskTargetB = 0.0f;
    float colorMaskHueWidth = 0.32f;
    float colorMaskFeather = 0.35f;
    float colorMaskMinChroma = 0.08f;
};

struct RawCropRotationRecipe {
    bool cropEnabled = false;
    float cropX = 0.0f;
    float cropY = 0.0f;
    float cropWidth = 1.0f;
    float cropHeight = 1.0f;
    int rotationDegrees = 0;
};

struct RawPreviewOutputRecipe {
    std::string previewIntent = "developed-preview";
    std::string internalViewTransform = "scene-linear-to-display";
    std::string outputColorSpace = "sRGB";
};

struct RawFinishToneRecipe {
    nlohmann::json layerJson;
};

struct RawViewTransformRecipe {
    nlohmann::json layerJson;
};

struct RawDevelopmentRecipe {
    int rawRecipeVersion = kRawDevelopmentRecipeVersion;
    RawSourceReference source;
    RawWhiteBalanceRecipe whiteBalance;
    float preToneExposureEv = 0.0f;
    RawLocalExposureRecipe localExposure;
    RawLocalRangeRecipe localRange;
    RawToneCurveRecipe toneCurve;
    RawFinishToneRecipe finishTone;
    RawViewTransformRecipe viewTransform;
    RawCropRotationRecipe cropRotation;
    RawPreviewOutputRecipe previewOutput;
    std::vector<std::string> stageOrder;
};

const std::vector<std::string>& DefaultStageOrder();
RawDevelopmentRecipe MakeDefaultRecipe(std::string sourcePath, std::string displayName = {});

const char* WhiteBalanceModeStableString(WhiteBalanceMode mode);
WhiteBalanceMode WhiteBalanceModeFromStableString(const std::string& value);
const char* ToneCurveModeStableString(ToneCurveMode mode);
ToneCurveMode ToneCurveModeFromStableString(const std::string& value);

nlohmann::json DefaultFinishToneJson();
nlohmann::json DefaultViewTransformJson();
nlohmann::json FinishToneJsonFromLegacyToneCurve(const RawToneCurveRecipe& toneCurve);
std::vector<RawLocalRangePoint> DefaultLocalRangePoints(float minEv = -8.0f, float maxEv = 6.0f);
RawLocalRangeRecipe DefaultLocalRangeRecipe();
RawLocalRangeRecipe SanitizeLocalRangeRecipe(RawLocalRangeRecipe localRange);
RawLocalRangeRecipe ApplyLocalRangePreset(RawLocalRangeRecipe localRange, RawLocalRangePreset preset);
RawLocalRangeRecipe LocalRangeRecipeFromLocalExposure(
    const RawLocalExposureRecipe& localExposure,
    const RawLocalRangeRecipe& baseLocalRange);
float EvaluateLocalRangeDeltaEv(const RawLocalRangeRecipe& localRange, float sceneEv);
float LocalRangeExposureScaleForLuma(const RawLocalRangeRecipe& localRange, float sceneLuma);
float EvaluateLocalRangeRegionMask(
    const RawLocalRangeRecipe& localRange,
    float normalizedX,
    float normalizedY,
    float sceneEv);
float EvaluateLocalRangeColorMask(
    const RawLocalRangeRecipe& localRange,
    float sceneR,
    float sceneG,
    float sceneB);
float EdgeAwareLocalRangeDeltaEvForSamples(
    const RawLocalRangeRecipe& localRange,
    float centerSceneEv,
    const std::vector<float>& sampleSceneEvs);
bool FinishStateEquals(const RawDevelopmentRecipe& a, const RawDevelopmentRecipe& b);
std::size_t FinishStateHash(const RawDevelopmentRecipe& recipe);
bool LocalRangeStateEquals(const RawDevelopmentRecipe& a, const RawDevelopmentRecipe& b);
std::size_t LocalRangeStateHash(const RawDevelopmentRecipe& recipe);

Raw::RawDevelopSettings ToRawDevelopSettings(const RawDevelopmentRecipe& recipe);
Raw::RawDetailFusionSettings ToRawDetailFusionSettings(const RawDevelopmentRecipe& recipe);
bool IsLocalExposureEnabled(const RawDevelopmentRecipe& recipe);
bool IsLocalRangeEnabled(const RawLocalRangeRecipe& localRange);
bool IsLocalRangeEnabled(const RawDevelopmentRecipe& recipe);
nlohmann::json SerializeRecipe(const RawDevelopmentRecipe& recipe);
RawDevelopmentRecipe DeserializeRecipe(const nlohmann::json& value);
std::string RecipeDisplayName(const RawDevelopmentRecipe& recipe);

} // namespace Stack::RawRecipe
