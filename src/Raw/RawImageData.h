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
    Bilinear,
    QualityPlaceholder
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
    DenoiseDifference
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
