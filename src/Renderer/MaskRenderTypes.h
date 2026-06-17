#pragma once

#include "Color/LutData.h"
#include "Editor/Layers/LayerBase.h"
#include "NeuralDenoise/NeuralDenoiseTypes.h"
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"
#include "Utils/SharedPixelBuffer.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class RenderMaskGeneratorKind {
    Solid,
    LinearGradient,
    RadialGradient,
    Noise
};

enum class RenderMaskCombineMode {
    Add,
    Subtract,
    Intersect,
    Exclude
};

enum class RenderCustomMaskObjectType {
    Rectangle,
    Ellipse,
    Polygon,
    FreeformPath
};

enum class RenderCustomMaskOperation {
    Add,
    Subtract,
    Intersect,
    Exclude
};

enum class RenderMaskUtilityKind {
    Invert,
    Levels,
    Threshold
};

enum class RenderImageToMaskKind {
    Luminance,
    SampledRange
};

enum class RenderImageGeneratorKind {
    SolidColor,
    ColorGradient,
    Square,
    Circle,
    Text
};

struct RenderMaskSettings {
    float value = 1.0f;
    float angle = 0.0f;
    float offset = 0.0f;
    float scale = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float radius = 0.45f;
    float feather = 0.2f;
    bool invert = false;
};

struct RenderMaskUtilitySettings {
    float blackPoint = 0.0f;
    float whitePoint = 1.0f;
    float gamma = 1.0f;
    float threshold = 0.5f;
    float softness = 0.0f;
    bool invert = false;
};

struct RenderImageToMaskSettings {
    float low = 0.0f;
    float high = 1.0f;
    float softness = 0.0f;
    bool invert = false;
    int sampleCount = 1;
    float sampleRgb[3] = { 0.5f, 0.5f, 0.5f };
    float sampleLuma = 0.5f;
    float extraSampleRgb[4][3] = {
        { 0.5f, 0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f },
        { 0.5f, 0.5f, 0.5f }
    };
    float extraSampleLuma[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    float sampleU = 0.5f;
    float sampleV = 0.5f;
    float toneSimilarity = 0.12f;
    float colorSimilarity = 0.18f;
    float regionRadius = 0.35f;
    float regionFeather = 0.35f;
    float edgeSensitivity = 0.45f;
    float localCoherence = 0.45f;
};

struct RenderImageGeneratorSettings {
    float colorA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float colorB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float angle = 0.0f;
    float offset = 0.0f;
    std::string text = "Text";
    float fontSize = 96.0f;
};

struct RenderCustomMaskPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct RenderCustomMaskObject {
    int id = 0;
    RenderCustomMaskObjectType type = RenderCustomMaskObjectType::Rectangle;
    RenderCustomMaskOperation operation = RenderCustomMaskOperation::Add;
    std::vector<RenderCustomMaskPoint> points;
    bool enabled = true;
    bool invert = false;
    float strength = 1.0f;
    float feather = 0.0f;
    float blur = 0.0f;
};

struct RenderCustomMaskPayload {
    int width = 1024;
    int height = 1024;
    std::vector<float> rasterLayer;
    std::vector<RenderCustomMaskObject> objects;
    bool invert = false;
    float blurRadius = 0.0f;
    float expandContract = 0.0f;
};

struct RenderMaskSource {
    int nodeId = -1;
    RenderMaskGeneratorKind kind = RenderMaskGeneratorKind::Solid;
    RenderMaskSettings settings;
};

struct RenderLayerStep {
    std::shared_ptr<LayerBase> layer;
    int maskNodeId = -1;
};

enum class RenderGraphNodeKind {
    Image,
    RawSource,
    RawNeuralDenoise,
    RawDecode,
    RawDevelop,
    RawDetailAutoMask,
    RawDetailFusion,
    HdrMerge,
    Lut,
    Layer,
    Output,
    MaskGenerator,
    MaskCombine,
    Mix,
    MaskUtility,
    ImageToMask,
    ImageGenerator,
    ChannelSplit,
    ChannelCombine,
    CustomMask,
    DataMath
};

enum class RenderMixBlendMode {
    Normal,
    Average,
    Add,
    Multiply,
    Screen,
    AlphaOver
};

enum class RenderDataMathMode {
    Clamp,
    Add,
    Subtract,
    Multiply,
    Divide,
    Average,
    Min,
    Max,
    Difference,
    Remap
};

struct RenderDataMathSettings {
    float constantA = 0.0f;
    float constantB = 1.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float outMin = 0.0f;
    float outMax = 1.0f;
};

struct RenderGraphImagePayload {
    SharedPixelBuffer pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
};

struct RenderGraphRawSourcePayload {
    std::string sourcePath;
    Raw::RawMetadata metadata;
    Raw::RawImageData embeddedRawData;
};

struct RenderGraphRawDevelopPayload {
    Raw::RawDevelopSettings settings;
    bool scenePrepEnabled = false;
    Raw::RawDetailFusionSettings scenePrepSettings;
    bool integratedToneEnabled = false;
    nlohmann::json integratedToneLayerJson;
};

struct RenderGraphRawDecodePayload {
    Raw::RawDevelopSettings settings;
};

struct RenderGraphRawNeuralDenoisePayload {
    NeuralDenoise::NeuralDenoiseSettings settings;
};

struct RenderGraphRawDetailFusionPayload {
    Raw::RawDetailFusionSettings settings;
};

struct RenderGraphRawDetailAutoMaskPayload {
    Raw::RawDetailFusionSettings settings;
};

struct RenderGraphHdrMergePayload {
    Raw::HdrMergeSettings settings;
};

using RenderGraphLutPayload = ColorLut::LutPayload;

struct ToneCurveAutoRewriteFeedback {
    bool valid = false;
    int nodeId = -1;
    std::uint64_t requestRevision = 0;
    std::size_t authoredStateHash = 0;
    nlohmann::json authoredLayerJson;
    bool statsValid = false;
    float shadowPercentile = 0.02f;
    float midtonePercentile = 0.18f;
    float highlightPercentile = 0.85f;
    float clippingRatio = 0.0f;
    float noiseRisk = 0.0f;
    float highlightPressure = 0.0f;
    float textureConfidence = 0.5f;
    float hdrSpreadEv = 0.0f;
    int sceneProfile = 0;
    float recommendedBaseEv = 0.0f;
    float recommendedLocalStrength = 1.05f;
    float recommendedShadowOpening = 1.20f;
    float recommendedHighlightCompression = 1.25f;
    float recommendedFoundationEv[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
};

struct RenderGraphNode {
    int nodeId = -1;
    std::uint64_t requestRevision = 0;
    RenderGraphNodeKind kind = RenderGraphNodeKind::Image;
    RenderGraphImagePayload image;
    RenderGraphRawSourcePayload rawSource;
    RenderGraphRawNeuralDenoisePayload rawNeuralDenoise;
    RenderGraphRawDecodePayload rawDecode;
    RenderGraphRawDevelopPayload rawDevelop;
    RenderGraphRawDetailAutoMaskPayload rawDetailAutoMask;
    RenderGraphRawDetailFusionPayload rawDetailFusion;
    RenderGraphHdrMergePayload hdrMerge;
    RenderGraphLutPayload lut;
    nlohmann::json layerJson;
    RenderMaskGeneratorKind maskKind = RenderMaskGeneratorKind::Solid;
    RenderMaskSettings maskSettings;
    RenderMaskCombineMode maskCombineMode = RenderMaskCombineMode::Intersect;
    RenderMaskUtilityKind maskUtilityKind = RenderMaskUtilityKind::Invert;
    RenderMaskUtilitySettings maskUtilitySettings;
    RenderImageToMaskKind imageToMaskKind = RenderImageToMaskKind::Luminance;
    RenderImageToMaskSettings imageToMaskSettings;
    RenderCustomMaskPayload customMask;
    RenderImageGeneratorKind imageGeneratorKind = RenderImageGeneratorKind::SolidColor;
    RenderImageGeneratorSettings imageGeneratorSettings;
    RenderMixBlendMode mixBlendMode = RenderMixBlendMode::Normal;
    float mixFactor = 0.5f;
    RenderDataMathMode dataMathMode = RenderDataMathMode::Clamp;
    RenderDataMathSettings dataMathSettings;
};

struct RenderGraphLink {
    int fromNodeId = -1;
    std::string fromSocketId;
    int toNodeId = -1;
    std::string toSocketId;
};

struct RenderGraphSnapshot {
    int outputNodeId = -1;
    std::string outputSocketId;
    bool autoGainMaskPreview = false;
    std::vector<RenderGraphNode> nodes;
    std::vector<RenderGraphLink> links;
};
