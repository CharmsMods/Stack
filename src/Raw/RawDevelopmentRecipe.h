#pragma once

#include "Raw/Denoise/RawDenoiseControlMap.h"
#include "Raw/RawImageData.h"
#include "Raw/RawGradientMask.h"
#include "Raw/RawColorCalibration.h"
#include "Raw/Tone/SceneTone.h"
#include "Raw/Detail/DetailContrast.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Stack::RawRecipe {

inline constexpr int kRawDevelopmentRecipeVersion = 27;
inline constexpr std::size_t kMaxRawGradientAdjustments = 16;
inline constexpr std::size_t kMaxRawPointCurvePoints = 12;
inline constexpr std::size_t kMaxRawLocalRangeTargetZones = 32;
inline constexpr std::size_t kMaxRawLocalRangeTargetSeeds = 32;
inline constexpr std::size_t kMaxRawColorWarpPins = 8;
inline constexpr std::size_t kMaxRawColorWarpSampleCircles = 16;
inline constexpr std::size_t kMaxRawColorWarpRegions = 8;
inline constexpr std::size_t kMaxRawColorWarpLinkGroups = 8;
inline constexpr float kRawColorWarpEvMinimum = -16.0f;
inline constexpr float kRawColorWarpEvMaximum = 16.0f;
inline constexpr std::size_t kRawColorWarpEvCurveSampleCount = 257;
inline constexpr const char* kFinishToneRangeExtendedSceneV1 =
    "extended-scene-v1";
inline constexpr const char* kRestormerDenoisePackageId =
    "stack-restormer-denoise-v1";
inline constexpr const char* kRestormerDenoiseAdapterVersion =
    "restormer-rgb-adapter-v2";

enum class WhiteBalanceMode {
    AsShot,
    CustomMultipliers
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
    bool enabled = true;
    RawRgbDenoiseMethod method = RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    RawRgbDenoiseMapping mapping = RawRgbDenoiseMapping::SceneLinearSafeV1;
    std::string packageId = kRestormerDenoisePackageId;
    std::string packageVersion;
    std::string modelSha256;
    std::string adapterVersion = kRestormerDenoiseAdapterVersion;
    float colorNoise = 0.35f;
    float luminanceNoise = 0.20f;
    float detailProtection = 0.75f;
    float edgeSensitivity = 0.70f;
    float maximumStructureSize = 128.0f;
    RawDenoiseControlMap lumaMap { 0.0f };
    RawDenoiseControlMap chromaMap { 0.0f };
    std::uint64_t nextControlPointId = 1;

    // View-only fields. RAW Lab writes these into render snapshots and does
    // not store them as authored project state.
    RawDenoiseDiagnosticMode diagnosticMode =
        RawDenoiseDiagnosticMode::None;
    RawDenoiseMapLayer diagnosticLayer = RawDenoiseMapLayer::Luma;
    std::uint64_t diagnosticPointId = 0;
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
    bool hasMultipliers = false;
    std::array<float, 3> multipliers { 1.0f, 1.0f, 1.0f };
};

struct RawToneCurvePoint {
    float input = 0.0f;
    float output = 0.0f;
};

// Curve handles are stored as normalized offsets from their anchor point so
// moving an anchor carries its manually authored handle with it. Strength is
// normalized to the UI's 0%-150% range: 0.0 is straight, 1.0 is the normal
// smooth/custom handle, and 1.5 is an exact neighboring-tangent match.
struct RawBezierHandleState {
    float strength = 0.0f;
    bool manual = false;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

struct RawBezierCurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    RawBezierHandleState incoming;
    RawBezierHandleState outgoing;
};

struct RawBezierSegment {
    RawBezierCurvePoint left;
    RawBezierCurvePoint right;
    float leftHandleX = 0.0f;
    float leftHandleY = 0.0f;
    float rightHandleX = 0.0f;
    float rightHandleY = 0.0f;
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
    RawBezierHandleState incoming;
    RawBezierHandleState outgoing;
};

struct RawPointCurveComponent {
    std::string interpolation = "bezier-segments-v1";
    std::vector<RawPointCurveControlPoint> basePoints;
    std::vector<RawPointCurveControlPoint> points;
};

struct RawPointCurveSet {
    int version = 2;
    std::array<RawPointCurveComponent, 4> curves;
};

struct RawToneCurveRecipe {
    ToneCurveMode mode = ToneCurveMode::Default;
    std::vector<RawToneCurvePoint> points;
};

struct RawLocalRangePoint {
    float ev = 0.0f;
    float deltaEv = 0.0f;
    RawBezierHandleState incoming;
    RawBezierHandleState outgoing;
};

struct RawLocalRangeTargetSeed {
    float sourceU = 0.5f;
    float sourceV = 0.5f;
};

// Area curve coordinates and manual handle offsets are measured in EV.
// Graph zoom is UI state and never changes these values.
struct RawZoneAreaPoint {
    float ev = 0.0f;
    float deltaEv = 0.0f;
    RawBezierHandleState incoming;
    RawBezierHandleState outgoing;
};

struct RawZoneBrushPoint {
    float u = 0.5f;
    float v = 0.5f;
};

struct RawZoneBrushStroke {
    bool erase = false;
    // Radius relative to the shorter source-image side, before user transforms.
    float radius = 0.06f;
    float softness = 1.0f;
    float opacity = 1.0f;
    std::vector<RawZoneBrushPoint> path;
    // Guidance samples the first point against the neutral development image.
    // Later points extend the footprint without teaching it a new background.
    bool followEdges = false;
    float edgeSensitivity = 0.65f;
};

struct RawZoneArea {
    std::string id;
    std::string name;
    bool enabled = true;
    float offsetEv = 0.0f;
    float sourceAspect = 1.0f;
    std::vector<RawZoneAreaPoint> points {{-8.0f, 0.0f}, {6.0f, 0.0f}};
    std::vector<RawZoneBrushStroke> strokes;
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
    int areasVersion = 1;
    std::vector<RawZoneArea> areas;
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

struct RawTechnicalRecipe {
    Raw::RawProcessingVersion processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    Raw::DemosaicMethod demosaicMethod = Raw::DemosaicMethod::MalvarHeCutler;
    Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
    bool applyBaselineExposure = true;
    bool encodeSrgbOutput = true;
    Raw::RawMosaicDenoiseSettings mosaicDenoise;
};

struct RawFinishToneRecipe {
    nlohmann::json layerJson;
};

struct RawGradientEvAdjustment {
    RawGradientMask mask;
    RawLocalRangeRecipe curve;
};

struct RawGradientToneAdjustment {
    RawGradientMask mask;
    nlohmann::json curveJson;
};

enum class RawColorWarpInterpretationMode {
    GuidedFamily = 0,
    DominantFamily,
    ConnectedFamily,
    MultipleColors,
    ColorOnly,
    ColorAndBrightness,
    FullContents
};

enum class RawColorWarpSamplePolarity {
    Include = 0,
    Exclude
};

enum class RawColorWarpSpatialMode {
    AllMatches = 0,
    Connected,
    Cohesive,
    EdgeAwareReach,
    AssistedRegion
};

enum class RawColorWarpFeatherDirection {
    Inward = 0,
    Centered,
    Outward
};

struct RawColorWarpEvCurve {
    // Samples are uniformly spaced from -16 EV through +16 EV. An empty
    // vector is the in-memory shorthand for the neutral all-EV curve and is
    // expanded by recipe sanitization before evaluation or serialization.
    std::vector<float> samples;
};

// Coordinates are in the oriented, full pre-output-crop Color Warp domain.
// radiusU/radiusV encode one source-pixel radius in normalized coordinates so
// a recalled sample stays circular even when source pixels are not square in
// display space.
struct RawColorWarpSampleCircle {
    std::string id;
    float centerU = 0.5f;
    float centerV = 0.5f;
    float radiusU = 0.02f;
    float radiusV = 0.02f;
    RawColorWarpInterpretationMode interpretation =
        RawColorWarpInterpretationMode::GuidedFamily;
    RawColorWarpSamplePolarity polarity = RawColorWarpSamplePolarity::Include;
};

struct RawColorWarpRegion {
    std::string id;
    std::vector<RawColorWarpSampleCircle> circles;
    RawColorWarpSpatialMode spatialMode = RawColorWarpSpatialMode::Cohesive;
    float reachPixels = 24.0f;
    float spatialSupport = 1.0f;
    float edgeStop = 0.65f;
    float featherPixels = 0.0f;
    RawColorWarpFeatherDirection featherDirection =
        RawColorWarpFeatherDirection::Centered;
};

struct RawColorWarpLinkGroup {
    std::string id;
    std::string name;
    std::vector<std::string> pinIds;
};

// Color Warp is authored in the two-dimensional OKLab opponent plane.
// source/target A/B are the actual OKLab a/b coordinates; lightness remains a
// separate qualifier/adjustment and is never encoded into the disc position.
struct RawColorWarpPin {
    std::string id;
    std::string name;
    bool enabled = true;
    bool protectColor = false;
    float sourceA = 0.0f;
    float sourceB = 0.0f;
    float targetA = 0.0f;
    float targetB = 0.0f;
    float radius = 0.12f;
    float softness = 0.55f;
    // Zero directionality is the exact historical circular qualifier.
    // At one, orientation/aperture form a rounded directional cone.
    float qualifierDirectionality = 0.0f;
    float qualifierOrientationRadians = 0.0f;
    float qualifierAperture = 0.45f;
    float strength = 1.0f;
    std::string regionId;
    RawColorWarpEvCurve evCurve;
    float lightnessDeltaEv = 0.0f;
};

struct RawColorWarpRecipe {
    int version = 2;
    bool enabled = true;
    float strength = 1.0f;
    std::vector<RawColorWarpPin> pins;
    std::vector<RawColorWarpRegion> regions;
    std::vector<RawColorWarpLinkGroup> linkGroups;
};

struct RawColorWarpCoordinate {
    float lightness = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
    float sceneEv = -16.0f;
};

struct RawViewTransformRecipe {
    nlohmann::json layerJson;
};

struct RawDevelopmentRecipe {
    int rawRecipeVersion = kRawDevelopmentRecipeVersion;
    bool requireFullSpatialInput = false; // Execution snapshot only, not authored.
    RawTechnicalRecipe technical;
    RawSourceReference source;
    RawWhiteBalanceRecipe whiteBalance;
    RawRgbDenoiseRecipe rgbDenoise;
    RawColorCalibrationRecipe colorCalibration;
    float preToneExposureEv = 0.0f;
    RawLocalRangeRecipe localRange;
    std::vector<RawGradientEvAdjustment> evGradients;
    RawFinishToneRecipe finishTone;
    std::vector<RawGradientToneAdjustment> toneGradients;
    RawColorWarpRecipe colorWarp;
    DetailContrast detailContrast;
    RawViewTransformRecipe viewTransform;
    RawCropRotationRecipe cropRotation;
    std::vector<std::string> stageOrder;
};

const std::vector<std::string>& DefaultStageOrder();
RawDevelopmentRecipe MakeDefaultRecipe(std::string sourcePath, std::string displayName = {});
RawDevelopmentRecipe BuildNeutralComparisonRecipe(
    const RawDevelopmentRecipe& currentRecipe);

const char* WhiteBalanceModeStableString(WhiteBalanceMode mode);
WhiteBalanceMode WhiteBalanceModeFromStableString(const std::string& value);
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
nlohmann::json SanitizeFinishTonePointCurveJson(nlohmann::json finishTone);
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
RawBezierSegment BuildRawBezierSegment(
    const std::vector<RawBezierCurvePoint>& points,
    std::size_t segmentIndex);
float EvaluateRawBezierCurve(
    const std::vector<RawBezierCurvePoint>& points,
    float x);
std::vector<RawBezierCurvePoint> RawPointCurveBezierPoints(
    const RawPointCurveComponent& component);
std::vector<RawBezierCurvePoint> RawLocalRangeBezierPoints(
    const RawLocalRangeRecipe& localRange);
std::array<float, 3> EvaluateFinishTonePointCurveRgb(
    const nlohmann::json& finishTone,
    const std::array<float, 3>& sceneRgb);
std::array<float, 3> EvaluateFinishTonePointCurveRgb(
    const nlohmann::json& finishTone,
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace);
bool IsIdentityRawPointCurveComponent(const RawPointCurveComponent& component);
const char* RawPointCurveChannelKey(RawPointCurveChannel channel);
nlohmann::json SerializeColorWarpRecipe(const RawColorWarpRecipe& colorWarp);
RawColorWarpRecipe DeserializeColorWarpRecipe(const nlohmann::json& value);
RawColorWarpRecipe SanitizeColorWarpRecipe(RawColorWarpRecipe colorWarp);
bool IsColorWarpEnabled(const RawColorWarpRecipe& colorWarp);
RawColorWarpEvCurve MakeUniformColorWarpEvCurve(float qualification = 1.0f);
RawColorWarpEvCurve MakeColorWarpEvCurveHump(
    float centerEv,
    float coreHalfWidthEv = 0.75f,
    float featherEv = 0.75f);
float EvaluateColorWarpEvCurve(
    const RawColorWarpEvCurve& curve,
    float sceneEv);
const char* RawColorWarpInterpretationModeStableString(
    RawColorWarpInterpretationMode mode);
RawColorWarpInterpretationMode RawColorWarpInterpretationModeFromStableString(
    const std::string& value);
const char* RawColorWarpSpatialModeStableString(RawColorWarpSpatialMode mode);
RawColorWarpSpatialMode RawColorWarpSpatialModeFromStableString(
    const std::string& value);
const char* RawColorWarpFeatherDirectionStableString(
    RawColorWarpFeatherDirection direction);
RawColorWarpFeatherDirection RawColorWarpFeatherDirectionFromStableString(
    const std::string& value);
float EvaluateColorWarpPinShapeDistance(
    const RawColorWarpPin& pin,
    float a,
    float b);
float EvaluateColorWarpPinShapeWeight(
    const RawColorWarpPin& pin,
    float a,
    float b);
float EvaluateColorWarpPinLightnessWeight(
    const RawColorWarpPin& pin,
    float sceneEv);
RawColorWarpCoordinate WorkingRgbToColorWarpCoordinate(
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace);
std::array<float, 3> ColorWarpCoordinateToWorkingRgb(
    const RawColorWarpCoordinate& coordinate,
    Raw::RawWorkingSpace workingSpace);
std::array<float, 3> ApplyColorWarp(
    const RawColorWarpRecipe& colorWarp,
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace);
// Fast path for interactive visualization after the caller has sanitized the
// recipe once. This avoids copying pin strings per sample.
std::array<float, 3> ApplyPreparedColorWarp(
    const RawColorWarpRecipe& colorWarp,
    const std::array<float, 3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace);
nlohmann::json DefaultViewTransformJson();
inline constexpr const char* kViewContrastModelPivotedLogV2 = "pivoted-log-v2";
float EvaluateViewTransformDisplayLuma(
    float input,
    float exposure,
    float blackEv,
    float whiteEv,
    float middleGrey,
    float shoulder,
    float toe,
    float contrast,
    float contrastPivotEv = 0.0f);
std::vector<RawLocalRangePoint> DefaultLocalRangePoints(float minEv = -8.0f, float maxEv = 6.0f);
RawLocalRangeRecipe DefaultLocalRangeRecipe();
RawLocalRangeRecipe SanitizeLocalRangeRecipe(RawLocalRangeRecipe localRange);
RawRgbDenoiseRecipe SanitizeRgbDenoiseRecipe(RawRgbDenoiseRecipe rgbDenoise);
bool HasRgbDenoiseEffect(const RawRgbDenoiseRecipe& rgbDenoise);
bool IsRgbDenoiseActive(const RawRgbDenoiseRecipe& rgbDenoise);
RawLocalRangeRecipe ApplyLocalRangePreset(RawLocalRangeRecipe localRange, RawLocalRangePreset preset);
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
bool IsLocalRangeEnabled(const RawLocalRangeRecipe& localRange);
bool IsLocalRangeEnabled(const RawDevelopmentRecipe& recipe);
bool IsViewTransformEnabled(const RawDevelopmentRecipe& recipe);
nlohmann::json SerializeRecipe(const RawDevelopmentRecipe& recipe);
RawDevelopmentRecipe DeserializeRecipe(const nlohmann::json& value);
std::string RecipeDisplayName(const RawDevelopmentRecipe& recipe);

} // namespace Stack::RawRecipe
