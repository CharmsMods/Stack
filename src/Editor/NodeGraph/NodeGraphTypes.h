#pragma once

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

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
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
    CustomMask,
    DataMath
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
    AlphaOver
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

enum class SocketType {
    Image,
    Mask,
    Value,
    Analysis,
    Raw
};

enum class SocketDirection {
    Input,
    Output
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
};

} // namespace EditorNodeGraph
