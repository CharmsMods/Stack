#pragma once

#include <array>
#include <cstdint>
#include <memory>
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

enum class RawProcessingVersion {
    TruthfulV2
};

enum class DemosaicMethod {
    Bilinear,
    MalvarHeCutler,
    NearestNeighbor,
    HamiltonAdams
};

enum class RawWorkingSpace {
    LinearSrgbD65,
    LinearRec2020D65
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

// Declares how an optional float Bayer mosaic enters the shared RAW
// developer. This prevents an HDR virtual exposure from being mistaken for
// an ordinary normalized RAW or an already-denoised MFD result.
enum class NormalizedMosaicInputContract {
    None,
    MfdReferencePreGain,
    HdrVirtualAnchorPreGain,
    BracketingPreGain
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

struct RawSensorRect {
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
};

struct DngNoiseProfilePlane {
    double shotScale = 0.0;
    double readNoiseVariance = 0.0;
};

enum class RawMosaicDenoiseMode {
    FixedThreshold = 0,
    DngNoiseProfile = 1
};

struct RawMosaicDenoiseSettings {
    bool enabled = false;
    // Use the DNG model when it is available.
    RawMosaicDenoiseMode mode = RawMosaicDenoiseMode::DngNoiseProfile;
    bool hotPixelSuppression = true;
    float hotPixelThreshold = 0.12f;
    // These are green-plane and red/blue-plane strengths in the pre-demosaic
    // CFA domain, not perceptual luminance/chroma controls.
    float lumaStrength = 0.35f;
    float chromaStrength = 0.55f;
    int radius = 2;
    float edgeProtection = 0.55f;
    int iterations = 1;
};

struct RawToneCurvePoint {
    float input = 0.0f;
    float output = 0.0f;
};

struct RawDetailFusionSettings {
    // Runtime color contract supplied by the node or RAW Development recipe.
    RawProcessingVersion processingVersion = RawProcessingVersion::TruthfulV2;
    RawWorkingSpace workingSpace = RawWorkingSpace::LinearSrgbD65;
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
    std::string sourceContentSha256;
    std::uint64_t sourceByteSize = 0;
    std::string cameraMake;
    std::string cameraModel;
    std::string dngUniqueCameraModel;
    int rawWidth = 0;
    int rawHeight = 0;
    int visibleWidth = 0;
    int visibleHeight = 0;
    int leftMargin = 0;
    int topMargin = 0;
    // TIFF/EXIF orientation 1..8. Legacy 0 means unspecified/upright.
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
    float focalLengthMm = 0.0f;
    float focusDistanceMeters = 0.0f;
    std::string lensModel;
    std::int64_t captureTimestamp = 0;
    bool hasExposureTime = false;
    bool hasIsoSpeed = false;
    bool hasApertureFNumber = false;
    bool hasFocalLength = false;
    bool hasFocusDistance = false;
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
    std::vector<float> dngBlackLevelValues;
    std::vector<float> dngBlackLevelDeltaH;
    std::vector<float> dngBlackLevelDeltaV;
    std::vector<std::uint16_t> dngLinearizationTable;
    std::vector<float> dngWhiteLevelValues;
    RawSensorRect dngActiveArea;
    bool hasDngActiveArea = false;
    std::vector<RawSensorRect> dngMaskedAreas;
    float dngLinearResponseLimit = 1.0f;
    bool hasDngLinearResponseLimit = false;
    float dngBaselineNoise = 1.0f;
    bool hasDngBaselineNoise = false;
    std::vector<DngNoiseProfilePlane> dngNoiseProfile;
    bool hasDngNoiseProfile = false;
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
    std::array<int, 3> dngOpcodeCount { 0, 0, 0 };
    std::array<int, 3> dngUnsupportedOpcodeCountByList { 0, 0, 0 };
    std::array<int, 3> dngAppliedOpcodeCountByList { 0, 0, 0 };
    bool hasDngProfileGainTableMap = false;
    bool hasDngProfileGainTableMap2 = false;
    std::vector<DngGainMapOpcode> dngGainMaps;
    std::string uploadFormat = "R16UI";
    std::string dngTypeStatus;
    int linearChannels = 0;
    RawSampleFormat linearSampleFormat = RawSampleFormat::Unknown;
    std::vector<std::string> warnings;
    std::string error;
};

struct RawDevelopSettings {
    RawProcessingVersion processingVersion = RawProcessingVersion::TruthfulV2;
    RawWorkingSpace workingSpace = RawWorkingSpace::LinearSrgbD65;
    bool applyBaselineExposure = false;
    bool encodeSrgbOutput = false;
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
    float falseColorSuppression = 0.0f;
    float defringeStrength = 0.0f;
    float highlightEdgeCleanup = 0.0f;
    int chromaRadius = 1;
    float preserveRealColor = 0.70f;
    float lateralRedCyan = 0.0f;
    float lateralBlueYellow = 0.0f;
    std::vector<RawToneCurvePoint> toneCurvePoints;
    RawMosaicDenoiseSettings mosaicDenoise;
};

struct RawImageData {
    RawMetadata metadata;
    // Stable decoded-content identity. Ordinary files derive this from the
    // source SHA-256 plus the decoder/calibration contract; virtual RAWs may
    // supply their own immutable graph identity. GPU and proxy caches use the
    // compact hash instead of rescanning multi-megabyte pixel buffers.
    std::string contentIdentity;
    std::uint64_t contentIdentityHash = 0u;
    std::string decoderIdentityVersion;
    // Decoded sensor/linear buffers store row zero at the top, before EXIF.
    std::vector<std::uint16_t> rawBuffer;
    std::vector<std::uint16_t> linearUInt16Buffer;
    std::vector<float> linearFloatBuffer;
    // Reconstructed camera RGB has already passed sensor normalization and
    // pointwise calibration. White balance and camera color conversion follow.
    bool reconstructedCameraRgb = false;
    // Geometric coverage, independent of sensor validity or clipping. Null is opaque.
    // Covered reconstructions store both color and coverage with row zero at the top.
    std::shared_ptr<const std::vector<float>> outputCoverage;
    // Optional packed Bayer samples that have already passed DNG
    // linearization and black/white normalization. Samples remain in the
    // camera-native, pre-white-balance and pre-gain-map domain. Keeping the
    // buffer shared lets an atomic multi-frame result enter the ordinary RAW
    // development pipeline without copying a full-resolution float mosaic.
    std::shared_ptr<const std::vector<float>> normalizedMosaicBuffer;
    std::uint64_t normalizedMosaicContentHash = 0u;
    NormalizedMosaicInputContract normalizedMosaicInputContract =
        NormalizedMosaicInputContract::None;

    // Optional immutable HDR evidence. All unpacked maps use the active RAW
    // mosaic dimensions except validityMask, which is one packed bit/sample.
    struct HdrSidecars {
        std::shared_ptr<const std::vector<float>> varianceProxy;
        std::shared_ptr<const std::vector<float>> mergeConfidence;
        std::shared_ptr<const std::vector<float>> effectiveSampleCount;
        std::shared_ptr<const std::vector<float>> recoveredHeadroomStops;
        std::shared_ptr<const std::vector<std::uint8_t>> validityMask;
        std::shared_ptr<const std::vector<std::uint8_t>> ownerFrame;
        std::shared_ptr<const std::vector<std::uint8_t>> flags;
        std::string geometricReferenceFrameId;
        std::string radiometricAnchorFrameId;
    };
    std::shared_ptr<const HdrSidecars> hdrSidecars;

    // Processor-neutral evidence carried by a virtual Bayer measurement when
    // it is fed into another MultiFrame node. Variance is in the same
    // camera-native pre-gain domain as normalizedMosaicBuffer. A missing
    // sidecar means the input is an original sensor capture whose variance is
    // resolved from its RAW metadata; it never means zero variance.
    struct MultiFrameMeasurementSidecars {
        // Sensor/interpolation noise propagated through the actual weights.
        std::shared_ptr<const std::vector<float>> variance;
        // Registration and radiometric/model risk used by fusion. This is
        // separate from random measurement noise and must not set denoise strength.
        std::shared_ptr<const std::vector<float>> fusionUncertaintyVariance;
        // The number of statistically independent measurements that actually
        // survived clipping and registration rejection for this output sample.
        std::shared_ptr<const std::vector<float>> effectiveSupport;
        std::shared_ptr<const std::vector<std::uint8_t>> validity;
        std::shared_ptr<const std::vector<std::uint8_t>> clipping;
        // Non-zero when at least one locally aligned alternate was rejected.
        std::shared_ptr<const std::vector<std::uint8_t>> localRejection;
        // Bracketing MeasurementFallbackReason encoded as one byte per sample.
        std::shared_ptr<const std::vector<std::uint8_t>> fallbackReason;
        std::vector<std::string> originalFrameIds;
        std::string evidenceIdentitySha256;
    };
    std::shared_ptr<const MultiFrameMeasurementSidecars>
        multiFrameMeasurementSidecars;
};

const char* CfaPatternName(CfaPattern pattern);
const char* WhiteBalanceModeName(WhiteBalanceMode mode);
const char* RawProcessingVersionName(RawProcessingVersion version);
const char* DemosaicMethodName(DemosaicMethod method);
const char* RawWorkingSpaceName(RawWorkingSpace workingSpace);
const char* RawPixelLayoutName(RawPixelLayout layout);
const char* RawSampleFormatName(RawSampleFormat format);
const char* NormalizedMosaicInputContractName(
    NormalizedMosaicInputContract contract);
const char* RawDebugViewName(RawDebugView view);
const char* RawCameraTransformSourceName(RawCameraTransformSource source);
const char* HighlightReconstructionModeName(HighlightReconstructionMode mode);
int DisplayWidth(const RawMetadata& metadata);
int DisplayHeight(const RawMetadata& metadata);

} // namespace Raw
