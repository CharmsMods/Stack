#pragma once

#include "Graph/LayerIdentity.h"
#include "Graph/OutputDependencies.h"
#include "Raw/RawGraphOperation.h"
#include "Editor/NodeGraph/NodeGraphPayloads.h"

#include <string>
#include <vector>

namespace EditorNodeGraph {

struct Node {
    int id = 0;
    std::string instanceUuid;
    Stack::GraphModel::NodeRole role = Stack::GraphModel::NodeRole::Ordinary;
    Stack::GraphModel::Endpoint reference;
    Stack::NodeMath::LogicalValueType referenceType = Stack::NodeMath::LogicalValueType::Invalid;
    NodeKind kind = NodeKind::Layer;
    Stack::RawRecipe::GraphOperation rawOperation;
    nlohmann::json rawCoverage = nlohmann::json::object();
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
    ValuePayload value;
    TechnicalImageSettings technicalImageSettings;
    ReformatSettings reformatSettings;
    CompoundPayload compound;
    FrequencyFilterSettings frequencyFilterSettings;
    FrequencyResponseSettings frequencyResponseSettings;
    FrequencyFftSettings frequencyFftSettings;
    FrequencyFftSettings frequencyIfftSettings;
    SpectrumViewSettings spectrumViewSettings;
    ApplyFrequencyResponseSettings applyFrequencyResponseSettings;
    CombineSpectraSettings combineSpectraSettings;
    FrequencyMaskShape frequencyMaskShape = FrequencyMaskShape::LowPass;
    FrequencyMaskSettings frequencyMaskSettings;
    SpectrumMathMode spectrumMathMode = SpectrumMathMode::Multiply;
    SpectrumMathSettings spectrumMathSettings;
    MagnitudePhaseMode magnitudePhaseMode = MagnitudePhaseMode::Magnitude;
    MagnitudePhaseSettings magnitudePhaseSettings;
    SpectrumAnalyzerMode spectrumAnalyzerMode = SpectrumAnalyzerMode::RadialEnergy;
    SpectrumAnalyzerSettings spectrumAnalyzerSettings;
    std::vector<std::string> exposedParameterIds;
    bool outputEnabled = true;
    OutputSettings outputSettings;
    ConstantChannelSettings constantChannelSettings;
    ImageCombineSettings imageCombineSettings;
    std::string definitionId;
    std::string definitionVersion;
    std::string definitionHash;
    bool definitionResolved = false;
    std::string definitionResolutionError;
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
    RawProjectFramePayload rawProjectFrame;
    MultiFrameDenoisePayload multiFrameDenoise;
    MultiFrameHdrPayload multiFrameHdr;
    RawProjectSourceSetPayload rawProjectSourceSet;
    LutPayload lut;
    CustomMaskPayload customMask;
};

struct Link {
    int fromNodeId = 0;
    std::string fromSocketId;
    int toNodeId = 0;
    std::string toSocketId;
    enum class Ownership {
        User,
        ManagedSourceBinding
    } ownership = Ownership::User;
    std::string bindingId;
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
