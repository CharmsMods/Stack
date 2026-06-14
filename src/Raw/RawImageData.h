#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw {

enum class CfaPattern {
    Unknown,
    RGGB,
    BGGR,
    GBRG,
    GRBG
};

enum class WhiteBalanceMode {
    AsShot,
    Auto,
    Neutral,
    Manual
};

enum class DemosaicMethod {
    Bilinear
};

enum class RawPixelLayout {
    Unknown,
    MosaicBayer,
    LinearRgb
};

enum class RawSampleFormat {
    Unknown,
    UInt16,
    Float32
};

enum class RawDecoderBackend {
    LibRaw,
    NativeExperimental,
    CompareDebug
};

enum class RawDebugView {
    FinalOutput,
    NormalizedMosaic,
    CfaFalseColor,
    DemosaicedCameraRgb,
    WhiteBalancedCameraRgb,
    CameraTransformedRgb,
    ClippedRawChannels,
    PreDenoiseMosaic,
    PostDenoiseMosaic,
    HotPixelMask,
    DenoiseDifference,
    FalseColorMask,
    DefringeMask,
    HighlightEdgeMask
};

enum class RawCameraTransformSource {
    LibRawRgbCam,
    DngAuto,
    DngForwardMatrix1,
    DngForwardMatrix2,
    DngColorMatrixInverse
};

enum class HighlightReconstructionMode {
    Off,
    ClipNeutral,
    Luminance,
    ColorReconstruction
};

enum class RawDetailFusionMode {
    ManualMask,
    AutoAnalyze,
    Hybrid
};

enum class RawDetailFusionDebugView {
    FinalImage,
    ExposureMap,
    Confidence,
    HighlightSafety,
    ShadowProtection,
    SampleSelection,
    SmoothGradient,
    TrueEdge,
    TextureDetail,
    DebandRisk,
    AutoRange,
    NoiseFloorSnr,
    HighlightHeadroom,
    ChannelSaturation,
    RejectedDetail
};

struct DngGainMapOpcode {
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    int plane = 0;
    int planes = 1;
    int rowPitch = 1;
    int colPitch = 1;
    int mapPointsV = 0;
    int mapPointsH = 0;
    int mapPlanes = 1;
    double mapSpacingV = 0.0;
    double mapSpacingH = 0.0;
    double mapOriginV = 0.0;
    double mapOriginH = 0.0;
    std::vector<float> gains;
};

struct RawMosaicDenoiseSettings {
    bool enabled = false;
    bool hotPixelSuppression = true;
    float hotPixelThreshold = 0.12f;
    float lumaStrength = 0.35f;
    float chromaStrength = 0.55f;
    int radius = 2;
    float edgeProtection = 0.55f;
    int iterations = 1;
};

struct RawDetailFusionSettings {
    RawDetailFusionMode mode = RawDetailFusionMode::AutoAnalyze;
    RawDetailFusionDebugView debugView = RawDetailFusionDebugView::FinalImage;
    bool autoSafetyEnabled = true;
    bool overrideMinEv = false;
    bool overrideMaxEv = false;
    bool overrideBaseEv = false;
    bool overrideNoiseProtection = false;
    bool overrideHighlightProtection = false;
    bool overrideShadowLiftLimit = false;
    bool overrideWellExposedTarget = false;
    float minEvBias = 0.0f;
    float maxEvBias = 0.0f;
    float baseEvBias = 0.0f;
    float noiseProtectionBias = 0.0f;
    float highlightProtectionBias = 0.0f;
    float shadowLiftLimitBias = 0.0f;
    float wellExposedTargetBias = 0.0f;
    float minEv = -1.50f;
    float maxEv = 1.50f;
    float baseEv = 0.0f;
    float strength = 0.85f;
    int sampleCount = 17;
    float baseRadiusPercent = 0.012f;
    float highlightProtection = 0.90f;
    float shadowLiftLimit = 0.65f;
    float noiseProtection = 0.60f;
    float detailWeight = 0.55f;
    float wellExposedTarget = 0.30f;
    float smoothGradientProtection = 0.85f;
    float textureSensitivity = 0.50f;
    float skyBias = 0.55f;
    bool invertMask = false;
    float maskBlackPoint = 0.0f;
    float maskWhitePoint = 1.0f;
    float maskGamma = 1.0f;
    int smoothnessRadius = 5;
    int smoothAreaRadius = 12;
    float edgeAwareness = 0.65f;
    float haloGuard = 0.90f;
    float maskDebandDither = 0.0f;
    float manualBlend = 0.5f;
};

enum class HdrMergeDebugView {
    FinalImage,
    Contribution,
    Clipping,
    NoiseLimited,
    AlignmentConfidence,
    MotionMask,
    RejectedSamples
};

enum class HdrMergeAlignmentMode {
    Off,
    Translation,
    WideTranslation
};

enum class HdrMergeExposureMode {
    Metadata,
    Manual
};

enum class HdrMergeReferenceMode {
    Auto,
    Frame1,
    Frame2,
    Frame3
};

enum class HdrMergeDeghostMode {
    Off,
    Low,
    Medium,
    High
};

enum class HdrMergeMotionPriority {
    PreserveReference,
    AverageCleanAreas
};

struct HdrMergeSettings {
    HdrMergeDebugView debugView = HdrMergeDebugView::FinalImage;
    int frameCount = 2;
    HdrMergeAlignmentMode alignmentMode = HdrMergeAlignmentMode::Off;
    HdrMergeExposureMode exposureMode = HdrMergeExposureMode::Metadata;
    HdrMergeReferenceMode referenceMode = HdrMergeReferenceMode::Auto;
    HdrMergeDeghostMode deghostMode = HdrMergeDeghostMode::Low;
    HdrMergeMotionPriority motionPriority = HdrMergeMotionPriority::PreserveReference;
    float manualExposureEv[3] = { 0.0f, -2.0f, 2.0f };
    float exposureOffsetEv[3] = { 0.0f, 0.0f, 0.0f };
    bool autoReliability = true;
    float clipThreshold = 0.98f;
    float clipFeather = 0.08f;
    float blackThreshold = 0.002f;
    float blackFeather = 0.018f;
    float readNoise = 0.002f;
    bool noiseAware = true;
};

struct RawMetadata {
    std::string sourcePath;
    std::string cameraMake;
    std::string cameraModel;
    std::string dngUniqueCameraModel;
    int rawWidth = 0;
    int rawHeight = 0;
    int visibleWidth = 0;
    int visibleHeight = 0;
    int leftMargin = 0;
    int topMargin = 0;
    int orientation = 0;
    int bitDepth = 0;
    CfaPattern cfaPattern = CfaPattern::Unknown;
    RawPixelLayout pixelLayout = RawPixelLayout::Unknown;
    bool mosaiced = true;
    bool isDng = false;
    float blackLevel = 0.0f;
    std::array<float, 4> perChannelBlack { 0.0f, 0.0f, 0.0f, 0.0f };
    float whiteLevel = 65535.0f;
    std::string blackLevelSource;
    std::string whiteLevelSource;
    std::string whiteBalanceSource;
    std::string cameraMatrixSource;
    float rawMinimum = 0.0f;
    float rawMaximum = 0.0f;
    float defaultWhiteClipPercent = 0.0f;
    std::array<float, 4> cameraWhiteBalance { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 4> daylightWhiteBalance { 1.0f, 1.0f, 1.0f, 1.0f };
    float exposureTimeSeconds = 0.0f;
    float isoSpeed = 0.0f;
    float apertureFNumber = 0.0f;
    std::int64_t captureTimestamp = 0;
    bool hasExposureTime = false;
    bool hasIsoSpeed = false;
    bool hasApertureFNumber = false;
    bool hasCaptureTimestamp = false;
    std::array<float, 9> cameraToSrgb {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    bool hasCameraMatrix = false;
    std::array<float, 3> dngAsShotNeutral { 0.0f, 0.0f, 0.0f };
    bool hasDngAsShotNeutral = false;
    std::array<float, 9> dngColorMatrix1 {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    std::array<float, 9> dngColorMatrix2 {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    std::array<float, 9> dngForwardMatrix1 {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    std::array<float, 9> dngForwardMatrix2 {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    bool hasDngColorMatrix1 = false;
    bool hasDngColorMatrix2 = false;
    bool hasDngForwardMatrix1 = false;
    bool hasDngForwardMatrix2 = false;
    int dngIlluminant1 = 0;
    int dngIlluminant2 = 0;
    int dngCompression = 0;
    int dngPhotometricInterpretation = 0;
    int dngCfaLayout = 0;
    std::array<int, 2> dngCfaRepeatPatternDim { 0, 0 };
    std::array<int, 4> dngCfaPattern { -1, -1, -1, -1 };
    std::array<int, 3> dngCfaPlaneColor { 0, 1, 2 };
    std::array<int, 2> dngBlackLevelRepeatDim { 0, 0 };
    std::array<float, 4> dngBlackLevelPattern { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 3> dngAnalogBalance { 1.0f, 1.0f, 1.0f };
    bool hasDngAnalogBalance = false;
    std::array<float, 9> dngCameraCalibration1 {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    std::array<float, 9> dngCameraCalibration2 {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    bool hasDngCameraCalibration1 = false;
    bool hasDngCameraCalibration2 = false;
    float dngBaselineExposure = 0.0f;
    bool hasDngBaselineExposure = false;
    int dngGainMapCount = 0;
    int dngUnsupportedOpcodeCount = 0;
    std::vector<DngGainMapOpcode> dngGainMaps;
    std::string uploadFormat = "R16UI";
    std::string dngTypeStatus;
    int linearChannels = 0;
    RawSampleFormat linearSampleFormat = RawSampleFormat::Unknown;
    std::vector<std::string> warnings;
    std::string error;
};

struct RawDevelopSettings {
    float exposureStops = 0.0f;
    WhiteBalanceMode whiteBalanceMode = WhiteBalanceMode::AsShot;
    std::array<float, 3> manualWhiteBalance { 1.0f, 1.0f, 1.0f };
    bool overrideBlackLevel = false;
    float blackLevelOverride = 0.0f;
    bool overrideWhiteLevel = false;
    float whiteLevelOverride = 65535.0f;
    HighlightReconstructionMode highlightMode = HighlightReconstructionMode::Off;
    float highlightStrength = 0.5f;
    float highlightThreshold = 0.98f;
    DemosaicMethod demosaicMethod = DemosaicMethod::Bilinear;
    bool cameraTransformEnabled = true;
    RawCameraTransformSource cameraTransformSource = RawCameraTransformSource::DngAuto;
    bool debugBypassCameraTransform = false;
    bool debugTransposeCameraMatrix = false;
    RawDebugView debugView = RawDebugView::FinalOutput;
    int rotationDegrees = 0;
    bool rotateToFitFrame = false;
    bool flipHorizontally = false;
    bool flipVertically = false;
    float falseColorSuppression = 0.25f;
    float defringeStrength = 0.30f;
    float highlightEdgeCleanup = 0.40f;
    int chromaRadius = 1;
    float preserveRealColor = 0.70f;
    float lateralRedCyan = 0.0f;
    float lateralBlueYellow = 0.0f;
    RawMosaicDenoiseSettings mosaicDenoise;
};

struct RawImageData {
    RawMetadata metadata;
    std::vector<std::uint16_t> rawBuffer;
    std::vector<std::uint16_t> linearUInt16Buffer;
    std::vector<float> linearFloatBuffer;
};

const char* CfaPatternName(CfaPattern pattern);
const char* WhiteBalanceModeName(WhiteBalanceMode mode);
const char* DemosaicMethodName(DemosaicMethod method);
const char* RawPixelLayoutName(RawPixelLayout layout);
const char* RawSampleFormatName(RawSampleFormat format);
const char* RawDebugViewName(RawDebugView view);
const char* RawCameraTransformSourceName(RawCameraTransformSource source);
const char* HighlightReconstructionModeName(HighlightReconstructionMode mode);
int DisplayWidth(const RawMetadata& metadata);
int DisplayHeight(const RawMetadata& metadata);

} // namespace Raw
