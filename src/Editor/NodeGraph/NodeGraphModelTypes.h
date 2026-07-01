#pragma once

#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/NodeGraphPayloads.h"

#include <string>
#include <vector>

namespace EditorNodeGraph {

struct Node {
    int id = 0;
    NodeKind kind = NodeKind::Layer;
    int layerIndex = -1;
    LayerType layerType = LayerType::Brightness;
    std::string typeId;
    std::string title;
    Vec2 position;
    bool expanded = true;
    ScopeKind scopeKind = ScopeKind::Histogram;
    MaskGeneratorKind maskKind = MaskGeneratorKind::Solid;
    MaskGeneratorSettings maskSettings;
    MaskCombineMode maskCombineMode = MaskCombineMode::Intersect;
    MaskUtilityKind maskUtilityKind = MaskUtilityKind::Invert;
    MaskUtilitySettings maskUtilitySettings;
    ImageToMaskKind imageToMaskKind = ImageToMaskKind::Luminance;
    ImageToMaskSettings imageToMaskSettings;
    ImageGeneratorKind imageGeneratorKind = ImageGeneratorKind::SolidColor;
    ImageGeneratorSettings imageGeneratorSettings;
    MixBlendMode mixBlendMode = MixBlendMode::Normal;
    float mixFactor = 0.5f;
    DataMathMode dataMathMode = DataMathMode::Clamp;
    DataMathSettings dataMathSettings;
    bool outputEnabled = true;
    ImagePayload image;
    RawSourcePayload rawSource;
    RawDevelopmentPayload rawDevelopment;
    RawNeuralDenoisePayload rawNeuralDenoise;
    RawDecodePayload rawDecode;
    RawDevelopPayload rawDevelop;
    RawDetailAutoMaskPayload rawDetailAutoMask;
    RawDetailFusionPayload rawDetailFusion;
    HdrMergePayload hdrMerge;
    MfsrPayload mfsr;
    LutPayload lut;
    CustomMaskPayload customMask;
};

struct Link {
    int fromNodeId = 0;
    std::string fromSocketId;
    int toNodeId = 0;
    std::string toSocketId;
};

struct NodeGroup {
    int id = 0;
    std::string title = "New Group";
    Vec2 position;
    Vec2 size;
};

struct CompletedChainInfo {
    int outputNodeId = -1;
    int terminalNodeId = -1;
    int sourceNodeId = -1;
    std::vector<int> nodeIds;
};

enum class LinkRole {
    Render,
    Scope
};

struct ValidationResult {
    bool valid = true;
    bool outputConnected = false;
    std::vector<std::string> messages;
};

} // namespace EditorNodeGraph
