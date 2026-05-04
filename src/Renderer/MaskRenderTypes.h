#pragma once

#include "Editor/Layers/LayerBase.h"
#include "ThirdParty/json.hpp"
#include <memory>
#include <string>
#include <vector>

enum class RenderMaskGeneratorKind {
    Solid,
    LinearGradient,
    RadialGradient,
    Noise
};

enum class RenderMaskUtilityKind {
    Invert,
    Levels,
    Threshold
};

enum class RenderImageToMaskKind {
    Luminance
};

enum class RenderImageGeneratorKind {
    SolidColor,
    ColorGradient
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
};

struct RenderImageGeneratorSettings {
    float colorA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float colorB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float angle = 0.0f;
    float offset = 0.0f;
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
    Layer,
    Output,
    MaskGenerator,
    Mix,
    MaskUtility,
    ImageToMask,
    ImageGenerator
};

enum class RenderMixBlendMode {
    Normal,
    Add,
    Multiply,
    Screen,
    AlphaOver
};

struct RenderGraphImagePayload {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
};

struct RenderGraphNode {
    int nodeId = -1;
    RenderGraphNodeKind kind = RenderGraphNodeKind::Image;
    RenderGraphImagePayload image;
    nlohmann::json layerJson;
    RenderMaskGeneratorKind maskKind = RenderMaskGeneratorKind::Solid;
    RenderMaskSettings maskSettings;
    RenderMaskUtilityKind maskUtilityKind = RenderMaskUtilityKind::Invert;
    RenderMaskUtilitySettings maskUtilitySettings;
    RenderImageToMaskKind imageToMaskKind = RenderImageToMaskKind::Luminance;
    RenderImageToMaskSettings imageToMaskSettings;
    RenderImageGeneratorKind imageGeneratorKind = RenderImageGeneratorKind::SolidColor;
    RenderImageGeneratorSettings imageGeneratorSettings;
    RenderMixBlendMode mixBlendMode = RenderMixBlendMode::Normal;
    float mixFactor = 0.5f;
};

struct RenderGraphLink {
    int fromNodeId = -1;
    std::string fromSocketId;
    int toNodeId = -1;
    std::string toSocketId;
};

struct RenderGraphSnapshot {
    int outputNodeId = -1;
    std::vector<RenderGraphNode> nodes;
    std::vector<RenderGraphLink> links;
};
