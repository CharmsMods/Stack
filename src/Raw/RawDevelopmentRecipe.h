#pragma once

#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Stack::RawRecipe {

inline constexpr int kRawDevelopmentRecipeVersion = 14;
inline constexpr std::size_t kMaxRawPointCurvePoints = 12;
inline constexpr std::size_t kMaxRawLocalRangeTargetZones = 32;
inline constexpr std::size_t kMaxRawLocalRangeTargetSeeds = 32;
inline constexpr const char* kRestormerDenoisePackageId =
    "stack-restormer-denoise-v1";
inline constexpr const char* kRestormerDenoiseAdapterVersion =
    "restormer-rgb-adapter-v2";

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

enum class RawLocalRangeTargetScope {
    SelectedAreas,
    AllMatches
};

enum class RawLocalRangeZoneCombineMode {
    Add,
    Strongest,
    Blend
};

enum class RawRgbDenoiseMethod {
    ClassicalMultiscaleV1,
    RestormerRealV1,
    RestormerGaussianBlindV1
};

enum class RawRgbDenoiseMapping {
    SceneLinearSafeV1,
    ProcessedRgbMatchV1
};

struct RawRgbDenoiseRecipe {
    bool enabled = false;
    RawRgbDenoiseMethod method = RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    RawRgbDenoiseMapping mapping = RawRgbDenoiseMapping::SceneLinearSafeV1;
    std::string packageId = kRestormerDenoisePackageId;
    std::string packageVersion;
    std::string modelSha256;
    std::string adapterVersion = kRestormerDenoiseAdapterVersion;
    float colorNoise = 0.35f;
    float luminanceNoise = 0.20f;
    float detailProtection = 0.75f;
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

enum class RawPointCurveChannel : int {
    Composite = 0,
    Red = 1,
    Green = 2,
    Blue = 3
};

struct RawPointCurveControlPoint {
    float x = 0.0f;
    float y = 0.0f;
    int shape = 1;
};

struct RawPointCurveComponent {
    std::string interpolation = "monotone-cubic-v1";
    std::vector<RawPointCurveControlPoint> basePoints;
    std::vector<RawPointCurveControlPoint> points;
};

struct RawPointCurveSet {
    int version = 1;
    std::array<RawPointCurveComponent, 4> curves;
    bool legacyLumaEnabled = false;
    RawPointCurveComponent legacyLuma;
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

struct RawLocalRangeTargetSeed {
    float sourceU = 0.5f;
    float sourceV = 0.5f;
};

struct RawLocalRangeTargetZone {
    std::string id;
    std::string name;
    bool enabled = true;
    float centerEv = 0.0f;
    float coreHalfWidthEv = 0.35f;
    float featherEv = 0.65f;
    float deltaEv = 0.0f;
    RawLocalRangeTargetScope scope = RawLocalRangeTargetScope::SelectedAreas;
    bool colorEnabled = false;
    float targetUPrime = 0.19783f;
    float targetVPrime = 0.46832f;
    float targetChroma = 0.0f;
    float colorRadius = 0.025f;
    float colorFeather = 0.035f;
    std::vector<RawLocalRangeTargetSeed> seeds;
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
    RawLocalRangeZoneCombineMode targetZoneCombineMode = RawLocalRangeZoneCombineMode::Add;
    std::vector<RawLocalRangeTargetZone> targetZones;
};

struct RawCropRotationRecipe {
    bool cropEnabled = false;
    float cropX = 0.0f;
    float cropY = 0.0f;
    float cropWidth = 1.0f;
    float cropHeight = 1.0f;
    int rotationDegrees = 0;
    bool flipHorizontally = false;
    bool flipVertically = false;
};

struct RawPreviewOutputRecipe {
    std::string previewIntent = "developed-preview";
    std::string internalViewTransform = "scene-linear-to-display";
    std::string outputColorSpace = "sRGB";
};

struct RawTechnicalRecipe {
    Raw::RawProcessingVersion processingVersion = Raw::RawProcessingVersion::TruthfulV1;
    Raw::DemosaicMethod demosaicMethod = Raw::DemosaicMethod::MalvarHeCutler;
    Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
    bool applyBaselineExposure = true;
    bool encodeSrgbOutput = true;
    Raw::RawMosaicDenoiseSettings mosaicDenoise;
};

struct RawFinishToneRecipe {
    nlohmann::json layerJson;
};

struct RawViewTransformRecipe {
    nlohmann::json layerJson;
};

struct RawDevelopmentRecipe {
    int rawRecipeVersion = kRawDevelopmentRecipeVersion;
    RawTechnicalRecipe technical;
    RawSourceReference source;
    RawWhiteBalanceRecipe whiteBalance;
    RawRgbDenoiseRecipe rgbDenoise;
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
const char* ProcessingVersionStableString(Raw::RawProcessingVersion version);
Raw::RawProcessingVersion ProcessingVersionFromStableString(const std::string& value);
const char* DemosaicMethodStableString(Raw::DemosaicMethod method);
Raw::DemosaicMethod DemosaicMethodFromStableString(const std::string& value);
const char* WorkingSpaceStableString(Raw::RawWorkingSpace workingSpace);
Raw::RawWorkingSpace WorkingSpaceFromStableString(const std::string& value);
const char* MosaicDenoiseModeStableString(Raw::RawMosaicDenoiseMode mode);
Raw::RawMosaicDenoiseMode MosaicDenoiseModeFromStableString(const std::string& value);
const char* RgbDenoiseMethodStableString(RawRgbDenoiseMethod method);
RawRgbDenoiseMethod RgbDenoiseMethodFromStableString(const std::string& value);
const char* RgbDenoiseMappingStableString(RawRgbDenoiseMapping mapping);
RawRgbDenoiseMapping RgbDenoiseMappingFromStableString(const std::string& value);
const char* LocalRangeTargetScopeStableString(RawLocalRangeTargetScope scope);
RawLocalRangeTargetScope LocalRangeTargetScopeFromStableString(const std::string& value);
const char* LocalRangeZoneCombineModeStableString(RawLocalRangeZoneCombineMode mode);
RawLocalRangeZoneCombineMode LocalRangeZoneCombineModeFromStableString(const std::string& value);

nlohmann::json DefaultFinishToneJson();
nlohmann::json DefaultPointCurveComponentJson();
nlohmann::json SanitizeFinishTonePointCurveJson(
    nlohmann::json finishTone,
    int storedRecipeVersion = kRawDevelopmentRecipeVersion);
RawPointCurveSet PointCurveSetFromFinishToneJson(const nlohmann::json& finishTone);
void StorePointCurveSetInFinishToneJson(
    nlohmann::json& finishTone,
    const RawPointCurveSet& curveSet);
RawPointCurveComponent PointCurveComponentFromFinishToneJson(
    const nlohmann::json& finishTone,
    RawPointCurveChannel channel);
void StorePointCurveComponentInFinishToneJson(
    nlohmann::json& finishTone,
    RawPointCurveChannel channel,
    const RawPointCurveComponent& component);
float EvaluateRawPointCurve(
    const std::vector<RawPointCurveControlPoint>& points,
    const std::string& interpolation,
    float x);
float EvaluateRawPointCurveComponent(const RawPointCurveComponent& component, float x);
std::array<float, 3> EvaluateFinishTonePointCurveRgb(
    const nlohmann::json& finishTone,
    const std::array<float, 3>& sceneRgb);
bool IsIdentityRawPointCurveComponent(const RawPointCurveComponent& component);
const char* RawPointCurveChannelKey(RawPointCurveChannel channel);
nlohmann::json DefaultViewTransformJson();
float EvaluateViewTransformDisplayLuma(
    float input,
    float exposure,
    float blackEv,
    float whiteEv,
    float middleGrey,
    float shoulder,
    float toe,
    float contrast);
nlohmann::json FinishToneJsonFromLegacyToneCurve(const RawToneCurveRecipe& toneCurve);
std::vector<RawLocalRangePoint> DefaultLocalRangePoints(float minEv = -8.0f, float maxEv = 6.0f);
RawLocalRangeRecipe DefaultLocalRangeRecipe();
RawLocalRangeRecipe SanitizeLocalRangeRecipe(RawLocalRangeRecipe localRange);
RawRgbDenoiseRecipe SanitizeRgbDenoiseRecipe(RawRgbDenoiseRecipe rgbDenoise);
RawLocalRangeRecipe ApplyLocalRangePreset(RawLocalRangeRecipe localRange, RawLocalRangePreset preset);
RawLocalRangeRecipe LocalRangeRecipeFromLocalExposure(
    const RawLocalExposureRecipe& localExposure,
    const RawLocalRangeRecipe& baseLocalRange);
float EvaluateLocalRangeControlDeltaEv(const RawLocalRangeRecipe& localRange, float sceneEv);
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
std::array<float, 3> SceneLinearRgbToUvChroma(
    float sceneR,
    float sceneG,
    float sceneB,
    Raw::RawWorkingSpace workingSpace);
float EvaluateLocalRangeTargetZoneTonalWeight(
    const RawLocalRangeTargetZone& zone,
    float sceneEv);
float EvaluateLocalRangeTargetZoneColorWeight(
    const RawLocalRangeTargetZone& zone,
    float sceneR,
    float sceneG,
    float sceneB,
    Raw::RawWorkingSpace workingSpace);
float EvaluateLocalRangeTargetZoneDeltaEv(
    const RawLocalRangeTargetZone& zone,
    float sceneEv,
    float sceneR,
    float sceneG,
    float sceneB,
    Raw::RawWorkingSpace workingSpace,
    float selectedAreaWeight = 1.0f);
float CombineLocalRangeTargetZoneDeltaEv(
    RawLocalRangeZoneCombineMode mode,
    const std::vector<float>& weightedDeltas,
    const std::vector<float>& weights);
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
bool IsViewTransformEnabled(const RawDevelopmentRecipe& recipe);
nlohmann::json SerializeRecipe(const RawDevelopmentRecipe& recipe);
RawDevelopmentRecipe DeserializeRecipe(const nlohmann::json& value);
std::string RecipeDisplayName(const RawDevelopmentRecipe& recipe);

} // namespace Stack::RawRecipe
