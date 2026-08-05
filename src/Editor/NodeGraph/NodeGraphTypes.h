#pragma once

#include "NodeMath/ContractTypes.h"
#include "NodeMath/OutputInspection.h"

#include <string>

namespace EditorNodeGraph {

inline constexpr const char* kImageInputSocketId = "imageIn";
inline constexpr const char* kRawInputSocketId = "rawIn";
inline constexpr const char* kMixInputASocketId = "imageA";
inline constexpr const char* kMixInputBSocketId = "imageB";
inline constexpr const char* kDataMathBaseInputSocketId = "baseIn";
inline constexpr const char* kMixFactorSocketId = "factor";
inline constexpr const char* kHdrMergeInput1SocketId = "image1";
inline constexpr const char* kHdrMergeInput2SocketId = "image2";
inline constexpr const char* kHdrMergeInput3SocketId = "image3";
inline constexpr const char* kMfsrReferenceInputSocketId = "reference";
inline constexpr const char* kMaskInputSocketId = "maskIn";
inline constexpr const char* kMaskCombineInputASocketId = "maskA";
inline constexpr const char* kMaskCombineInputBSocketId = "maskB";
inline constexpr const char* kImageOutputSocketId = "imageOut";
inline constexpr const char* kPreFinishImageOutputSocketId = "preFinishImageOut";
inline constexpr const char* kRawOutputSocketId = "rawOut";
inline constexpr const char* kMaskOutputSocketId = "maskOut";
inline constexpr const char* kValueOutputSocketId = "valueOut";
inline constexpr const char* kExposureValueInputSocketId = "evIn";
inline constexpr const char* kChannelInputSocketId = "channelIn";
inline constexpr const char* kChannelOutputSocketId = "channelOut";
inline constexpr const char* kMatchExtentInputSocketId = "matchExtent";
inline constexpr const char* kSpectrumInputSocketId = "spectrumIn";
inline constexpr const char* kSpectrumOutputSocketId = "spectrumOut";
inline constexpr const char* kSpectrumInputASocketId = "spectrumA";
inline constexpr const char* kSpectrumInputBSocketId = "spectrumB";
inline constexpr const char* kFrequencyResponseInputSocketId = "responseIn";
inline constexpr const char* kFrequencyResponseOutputSocketId = "responseOut";
inline constexpr const char* kSpectrumMagnitudeInputSocketId = "magnitudeIn";
inline constexpr const char* kSpectrumMagnitudeOutputSocketId = "magnitudeOut";
inline constexpr const char* kSpectrumPhaseInputSocketId = "phaseIn";
inline constexpr const char* kSpectrumPhaseOutputSocketId = "phaseOut";
inline constexpr const char* kRadialPowerOutputSocketId = "radialPowerOut";
inline constexpr const char* kBandPowerOutputSocketId = "bandPowerOut";
inline constexpr const char* kPeakFrequencyOutputSocketId = "peakFrequencyOut";
inline constexpr const char* kPeakDirectionOutputSocketId = "peakDirectionOut";
inline constexpr const char* kStrengthParameterId = "strength";
inline constexpr const char* kLowCutoffParameterId = "lowCutoff";
inline constexpr const char* kHighCutoffParameterId = "highCutoff";
inline constexpr const char* kTransitionWidthParameterId = "transitionWidth";
inline constexpr const char* kButterworthOrderParameterId = "butterworthOrder";
inline constexpr const char* kAnalyzerLowParameterId = "bandLow";
inline constexpr const char* kAnalyzerHighParameterId = "bandHigh";

inline std::string ParameterInputSocketId(const std::string& parameterId) {
    return "param:" + parameterId;
}
inline std::string FrequencyNotchParameterId(
    const std::string& notchId,
    const std::string& field) {
    return "notch." + notchId + "." + field;
}
inline constexpr const char* kReductionFieldInputSocketId = "fieldIn";
inline constexpr const char* kReformatInputSocketId = "imageIn";
inline constexpr const char* kMaskUtilityInputSocketId = "maskIn";
inline constexpr const char* kImageToMaskInputSocketId = "imageIn";
inline constexpr const char* kScopeInputSocketId = "scopeIn";
inline constexpr const char* kPreviewInputSocketId = "previewIn";
inline constexpr int kMaxDataMathInputCount = 8;
inline constexpr int kMaxMfsrInputCount = 8;

inline int DataMathInputSocketIndex(const std::string& socketId) {
    if (socketId == kMixInputASocketId) {
        return 0;
    }
    if (socketId == kMixInputBSocketId) {
        return 1;
    }
    if (socketId.size() == 6 &&
        socketId[0] == 'i' &&
        socketId[1] == 'm' &&
        socketId[2] == 'a' &&
        socketId[3] == 'g' &&
        socketId[4] == 'e' &&
        socketId[5] >= 'C' &&
        socketId[5] < static_cast<char>('A' + kMaxDataMathInputCount)) {
        return static_cast<int>(socketId[5] - 'A');
    }
    return -1;
}

inline bool IsDataMathInputSocketId(const std::string& socketId) {
    return DataMathInputSocketIndex(socketId) >= 0;
}

inline std::string DataMathInputSocketId(int index) {
    if (index < 0 || index >= kMaxDataMathInputCount) {
        return {};
    }
    if (index == 0) {
        return kMixInputASocketId;
    }
    if (index == 1) {
        return kMixInputBSocketId;
    }
    std::string socketId = "image";
    socketId.push_back(static_cast<char>('A' + index));
    return socketId;
}

inline std::string DataMathInputSocketLabel(int index) {
    if (index < 0) {
        return "Data";
    }
    if (index < 26) {
        std::string label = "Data ";
        label.push_back(static_cast<char>('A' + index));
        return label;
    }
    return "Data " + std::to_string(index + 1);
}

inline int MfsrInputSocketIndex(const std::string& socketId) {
    if (socketId == kMfsrReferenceInputSocketId) {
        return 0;
    }
    constexpr const char* prefix = "frame";
    constexpr int prefixLength = 5;
    if (socketId.size() <= prefixLength || socketId.compare(0, prefixLength, prefix) != 0) {
        return -1;
    }

    int frameNumber = 0;
    for (std::size_t i = prefixLength; i < socketId.size(); ++i) {
        const char ch = socketId[i];
        if (ch < '0' || ch > '9') {
            return -1;
        }
        frameNumber = frameNumber * 10 + static_cast<int>(ch - '0');
    }
    if (frameNumber < 2 || frameNumber > kMaxMfsrInputCount) {
        return -1;
    }
    return frameNumber - 1;
}

inline bool IsMfsrInputSocketId(const std::string& socketId) {
    return MfsrInputSocketIndex(socketId) >= 0;
}

inline std::string MfsrInputSocketId(int index) {
    if (index < 0 || index >= kMaxMfsrInputCount) {
        return {};
    }
    if (index == 0) {
        return kMfsrReferenceInputSocketId;
    }
    return "frame" + std::to_string(index + 1);
}

inline std::string MfsrInputSocketLabel(int index) {
    if (index == 0) {
        return "Reference";
    }
    if (index > 0) {
        return "Frame " + std::to_string(index + 1);
    }
    return "Frame";
}

inline std::string MfdFrameInputSocketId(const std::string& frameId) {
    return frameId.empty() ? std::string() : "frame/" + frameId;
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct GraphRect {
    Vec2 min;
    Vec2 max;
};

enum class NodeKind {
    Image,
    RawSource,
    RawDevelopment,
    RawNeuralDenoise,
    RawDecode,
    RawDevelop,
    RawDetailAutoMask,
    RawDetailFusion,
    HdrMerge,
    Mfsr,
    Lut,
    Layer,
    Output,
    Composite,
    Scope,
    MaskGenerator,
    MaskCombine,
    Mix,
    Preview,
    MaskUtility,
    ImageToMask,
    ImageGenerator,
    ChannelSplit,
    ChannelCombine,
    ConstantChannel,
    CustomMask,
    DataMath,
    Value,
    TechnicalImage,
    Compound,
    FrequencyFilter,
    FrequencyResponse,
    FrequencyFft,
    FrequencyIfft,
    SpectrumView,
    ApplyFrequencyResponse,
    CombineSpectra,
    SpectrumSeparate,
    SpectrumRecombine,
    FrequencyMask,
    SpectrumMath,
    MagnitudePhase,
    SpectrumAnalyzer,
    FieldMean,
    Reformat,
    RawProjectFrame,
    MultiFrameDenoise,
    RawProjectSourceSet,
    Count
};

enum class ScopeKind {
    Histogram,
    Vectorscope,
    RGBParade
};

enum class MaskGeneratorKind {
    Solid,
    LinearGradient,
    RadialGradient,
    Noise
};

enum class MaskUtilityKind {
    Invert,
    Levels,
    Threshold
};

enum class MaskCombineMode {
    Add,
    Subtract,
    Intersect,
    Exclude
};

enum class CustomMaskReferenceMode {
    CustomSize,
    GraphNode
};

enum class CustomMaskObjectType {
    Rectangle,
    Ellipse,
    Polygon,
    FreeformPath
};

enum class CustomMaskOperation {
    Add,
    Subtract,
    Intersect,
    Exclude
};

enum class CustomMaskTool {
    Brush,
    Erase,
    Select,
    Rectangle,
    Ellipse,
    Polygon,
    FreeformPath
};

enum class ImageToMaskKind {
    Luminance,
    SampledRange
};

enum class ImageGeneratorKind {
    SolidColor,
    ColorGradient,
    Square,
    Circle,
    Text
};

enum class MixBlendMode {
    Normal,
    Average,
    Add,
    Multiply,
    Screen,
    StraightSourceOver,
    PremultipliedSourceOver
};

enum class DataMathMode {
    Clamp,
    Add,
    Subtract,
    Multiply,
    Divide,
    Average,
    Min,
    Max,
    Difference,
    Remap,
    ImageAverage
};

enum class SpectrumViewLut {
    Turbo,
    Viridis,
    Inferno,
    Grayscale
};

enum class FrequencyMaskShape {
    LowPass,
    HighPass,
    BandPass,
    BandStop,
    Notch,
    Gaussian,
    Butterworth
};

enum class SpectrumMathMode {
    Multiply,
    Add,
    Subtract,
    Difference
};

enum class MagnitudePhaseMode {
    Magnitude,
    Phase,
    Recombine
};

enum class SpectrumAnalyzerMode {
    RadialEnergy,
    DominantFrequency
};

enum class FrequencyFilterMode {
    AllPass,
    LowPass,
    HighPass,
    BandPass,
    BandStop,
    NotchReject
};

enum class FrequencyTransitionProfile {
    Smooth,
    Gaussian,
    Butterworth,
    Hard
};

enum class FrequencyEdgePolicy {
    Mirror,
    Wrap,
    ZeroPad
};

enum class SpectrumCombineMode {
    Add,
    Subtract
};

enum class SpectrumViewMode {
    Magnitude,
    Phase,
    Real,
    Imaginary
};

enum class SocketType {
    Image,
    ImageOrChannel,
    Channel,
    Spectrum,
    FrequencyResponse,
    SpectrumMagnitude,
    SpectrumPhase,
    Mask,
    ScalarField,
    Boolean,
    Integer,
    Scalar,
    Vector2,
    Vector3,
    Vector4,
    Matrix3,
    Matrix4,
    Curve,
    Coordinate,
    Histogram,
    Statistics,
    Metadata,
    Handle,
    Value,
    Analysis,
    Raw
};

struct OutputSettings {
    static constexpr int kSchemaVersion = 1;

    Stack::NodeMath::OutputChannelViewMode channelViewMode =
        Stack::NodeMath::OutputChannelViewMode::Neutral;
};

struct ConstantChannelSettings {
    static constexpr int kSchemaVersion = 1;

    float value = 1.0f;
    bool generatedOpaqueAlpha = false;
};

struct ImageCombineSettings {
    static constexpr int kSchemaVersion = 1;

    bool autoAlphaSuppressed = false;
};

enum class SocketDirection {
    Input,
    Output
};

enum class SocketVisibilityTier {
    Required,
    CommonOptional,
    Advanced
};

enum class SocketPreviewIntent {
    None,
    MaskConnection,
    ImageConnection
};

struct SocketDefinition {
    std::string id;
    int nodeId = 0;
    SocketDirection direction = SocketDirection::Input;
    SocketType type = SocketType::Image;
    std::string label;
    bool optional = false;
    bool visible = true;
    Stack::NodeMath::LogicalValueType logicalType =
        Stack::NodeMath::LogicalValueType::Invalid;
    std::string semanticRoleKey;
    Stack::NodeMath::SemanticField<Stack::NodeMath::ChannelDescriptor> declaredChannels;
    Stack::NodeMath::SemanticField<Stack::NodeMath::UnitDescriptor> declaredUnits;
    SocketVisibilityTier visibilityTier = SocketVisibilityTier::Required;
};

} // namespace EditorNodeGraph
