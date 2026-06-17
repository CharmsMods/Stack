#pragma once

#include "Color/LutData.h"
#include "Editor/LayerRegistry.h"
#include "NeuralDenoise/NeuralDenoiseTypes.h"
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace EditorNodeGraph {

inline constexpr const char* kImageInputSocketId = "imageIn";
inline constexpr const char* kRawInputSocketId = "rawIn";
inline constexpr const char* kMixInputASocketId = "imageA";
inline constexpr const char* kMixInputBSocketId = "imageB";
inline constexpr const char* kMixFactorSocketId = "factor";
inline constexpr const char* kHdrMergeInput1SocketId = "image1";
inline constexpr const char* kHdrMergeInput2SocketId = "image2";
inline constexpr const char* kHdrMergeInput3SocketId = "image3";
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

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

enum class NodeKind {
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
    Remap
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

struct SocketDefinition {
    std::string id;
    int nodeId = 0;
    SocketDirection direction = SocketDirection::Input;
    SocketType type = SocketType::Image;
    std::string label;
    bool optional = false;
    bool visible = true;
};

struct ImagePayload {
    std::string label;
    std::string sourcePath;
    std::vector<unsigned char> pngBytes;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
    mutable std::shared_ptr<const std::vector<unsigned char>> sharedPixels;
    mutable std::size_t pixelsFingerprint = 0;
};

inline void InvalidateImagePayloadRuntime(ImagePayload& payload) {
    payload.sharedPixels.reset();
    payload.pixelsFingerprint = 0;
}

struct RawSourcePayload {
    std::string label;
    std::string sourcePath;
    Raw::RawMetadata metadata;
};

using LutPayload = ColorLut::LutPayload;

enum class RawDevelopUiMode {
    Auto = 0,
    Manual = 1
};

enum class DevelopAutoIntent {
    NaturalFinished = 0,
    CleanBase,
    FlatEditingBase,
    BrightNatural,
    DarkNatural,
    PunchyHighContrast,
    MaximumRangeDetail
};

inline const char* DevelopAutoIntentStableString(DevelopAutoIntent intent) {
    switch (intent) {
        case DevelopAutoIntent::CleanBase: return "CleanBase";
        case DevelopAutoIntent::FlatEditingBase: return "FlatEditingBase";
        case DevelopAutoIntent::BrightNatural: return "BrightNatural";
        case DevelopAutoIntent::DarkNatural: return "DarkNatural";
        case DevelopAutoIntent::PunchyHighContrast: return "PunchyHighContrast";
        case DevelopAutoIntent::MaximumRangeDetail: return "MaximumRangeDetail";
        case DevelopAutoIntent::NaturalFinished:
        default:
            return "NaturalFinished";
    }
}

inline const char* DevelopAutoIntentLabel(DevelopAutoIntent intent) {
    switch (intent) {
        case DevelopAutoIntent::CleanBase: return "Clean Base";
        case DevelopAutoIntent::FlatEditingBase: return "Flat Editing Base";
        case DevelopAutoIntent::BrightNatural: return "Bright Natural";
        case DevelopAutoIntent::DarkNatural: return "Dark Natural";
        case DevelopAutoIntent::PunchyHighContrast: return "Punchy / High Contrast";
        case DevelopAutoIntent::MaximumRangeDetail: return "Maximum Range / Detail";
        case DevelopAutoIntent::NaturalFinished:
        default:
            return "Natural Finished";
    }
}

inline const char* DevelopAutoIntentDescription(DevelopAutoIntent intent) {
    switch (intent) {
        case DevelopAutoIntent::CleanBase:
            return "Technically clean, conservative starting point for later editing.";
        case DevelopAutoIntent::FlatEditingBase:
            return "Brings useful mids/detail into visible range and lowers final contrast so manual editing is easier.";
        case DevelopAutoIntent::BrightNatural:
            return "Realistic but biased toward a brighter rendered result.";
        case DevelopAutoIntent::DarkNatural:
            return "Preserves darker scene mood and avoids forcing low-key images into gray mids.";
        case DevelopAutoIntent::PunchyHighContrast:
            return "Stronger visual separation and contrast, while still avoiding fake HDR.";
        case DevelopAutoIntent::MaximumRangeDetail:
            return "Prioritizes fitting more highlight/shadow information into visible range without claiming to recover missing clipped data.";
        case DevelopAutoIntent::NaturalFinished:
        default:
            return "Balanced realistic output intended to look usable immediately.";
    }
}

inline DevelopAutoIntent DevelopAutoIntentFromStableString(const std::string& value) {
    if (value == "CleanBase") return DevelopAutoIntent::CleanBase;
    if (value == "FlatEditingBase") return DevelopAutoIntent::FlatEditingBase;
    if (value == "BrightNatural") return DevelopAutoIntent::BrightNatural;
    if (value == "DarkNatural") return DevelopAutoIntent::DarkNatural;
    if (value == "PunchyHighContrast") return DevelopAutoIntent::PunchyHighContrast;
    if (value == "MaximumRangeDetail") return DevelopAutoIntent::MaximumRangeDetail;
    return DevelopAutoIntent::NaturalFinished;
}

struct DevelopAutoGuidance {
    DevelopAutoIntent intent = DevelopAutoIntent::NaturalFinished;
    float autoStrength = 0.78f;
    float exposureBias = 0.0f;
    float dynamicRange = 1.0f;
    float shadowLift = 0.0f;
    float highlightGuard = 0.0f;
    float highlightCharacter = 0.0f;
    float contrastBias = 0.0f;
    // Guide 05 user intent axes. Neutral keeps automatic weak subject/scene evidence in charge.
    float subjectSceneBias = 0.0f;      // -1 global scene integrity, +1 marked/likely subject priority
    float moodReadabilityBias = 0.0f;   // -1 preserve mood, +1 improve subject/readability
};

enum class DevelopSubjectImportanceMode {
    Important = 0,
    Reveal,
    Protect,
    PreserveMood,
    Ignore
};

inline const char* DevelopSubjectImportanceModeStableString(DevelopSubjectImportanceMode mode) {
    switch (mode) {
        case DevelopSubjectImportanceMode::Reveal: return "Reveal";
        case DevelopSubjectImportanceMode::Protect: return "Protect";
        case DevelopSubjectImportanceMode::PreserveMood: return "PreserveMood";
        case DevelopSubjectImportanceMode::Ignore: return "Ignore";
        case DevelopSubjectImportanceMode::Important:
        default:
            return "Important";
    }
}

inline const char* DevelopSubjectImportanceModeLabel(DevelopSubjectImportanceMode mode) {
    switch (mode) {
        case DevelopSubjectImportanceMode::Reveal: return "Reveal";
        case DevelopSubjectImportanceMode::Protect: return "Protect";
        case DevelopSubjectImportanceMode::PreserveMood: return "Preserve Mood";
        case DevelopSubjectImportanceMode::Ignore: return "Ignore / Low Priority";
        case DevelopSubjectImportanceMode::Important:
        default:
            return "Important";
    }
}

inline const char* DevelopSubjectImportanceModeDescription(DevelopSubjectImportanceMode mode) {
    switch (mode) {
        case DevelopSubjectImportanceMode::Reveal:
            return "Make this region more visible when quality allows.";
        case DevelopSubjectImportanceMode::Protect:
            return "Protect this region from clipping, smearing, over-compression, or heavy cleanup.";
        case DevelopSubjectImportanceMode::PreserveMood:
            return "Let this region keep darker or brighter scene mood instead of forcing neutral readability.";
        case DevelopSubjectImportanceMode::Ignore:
            return "Spend less exposure, range, or cleanup budget here.";
        case DevelopSubjectImportanceMode::Important:
        default:
            return "This region matters; Auto should weigh it more strongly without treating it as a hard mask.";
    }
}

inline DevelopSubjectImportanceMode DevelopSubjectImportanceModeFromStableString(const std::string& value) {
    if (value == "Reveal") return DevelopSubjectImportanceMode::Reveal;
    if (value == "Protect") return DevelopSubjectImportanceMode::Protect;
    if (value == "PreserveMood") return DevelopSubjectImportanceMode::PreserveMood;
    if (value == "Ignore") return DevelopSubjectImportanceMode::Ignore;
    return DevelopSubjectImportanceMode::Important;
}

struct DevelopSubjectImportanceRegion {
    int id = 0;
    DevelopSubjectImportanceMode mode = DevelopSubjectImportanceMode::Important;
    bool enabled = true;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float radiusX = 0.18f;
    float radiusY = 0.18f;
    float feather = 0.35f;
    float strength = 0.75f;
};

struct DevelopSubjectImportanceStrokePoint {
    float x = 0.5f;
    float y = 0.5f;
};

struct DevelopSubjectImportanceStroke {
    int id = 0;
    DevelopSubjectImportanceMode mode = DevelopSubjectImportanceMode::Important;
    bool enabled = true;
    bool subtract = false;
    float radius = 0.045f;
    float feather = 0.35f;
    float strength = 0.75f;
    std::vector<DevelopSubjectImportanceStrokePoint> points;
};

struct DevelopSubjectImportanceMap {
    int schemaVersion = 2;
    bool enabled = false;
    bool showOverlay = true;
    float overlayOpacity = 0.45f;
    bool showInterpretedMapOverlay = false;
    float interpretedMapOpacity = 0.32f;
    bool showRefinedMapOverlay = false;
    float refinedMapOpacity = 0.36f;
    bool brushEnabled = false;
    bool brushSubtract = false;
    DevelopSubjectImportanceMode brushMode = DevelopSubjectImportanceMode::Important;
    float brushRadius = 0.045f;
    float brushFeather = 0.35f;
    float brushStrength = 0.75f;
    int activeRegionId = 0;
    int activeStrokeId = 0;
    int nextRegionId = 1;
    int nextStrokeId = 1;
    std::vector<DevelopSubjectImportanceRegion> regions;
    std::vector<DevelopSubjectImportanceStroke> strokes;
};

struct RawDevelopPayload {
    Raw::RawDevelopSettings settings;
    bool scenePrepEnabled = true;
    Raw::RawDetailFusionSettings scenePrepSettings;
    bool integratedToneEnabled = true;
    nlohmann::json integratedToneLayerJson;
    DevelopAutoGuidance autoGuidance;
    DevelopSubjectImportanceMap subjectImportance;
    RawDevelopUiMode uiMode = RawDevelopUiMode::Auto;
};

struct RawDecodePayload {
    Raw::RawDevelopSettings settings;
};

struct RawNeuralDenoisePayload {
    NeuralDenoise::NeuralDenoiseSettings settings;
};

struct RawDetailFusionPayload {
    Raw::RawDetailFusionSettings settings;
};

struct RawDetailAutoMaskPayload {
    Raw::RawDetailFusionSettings settings;
};

struct HdrMergePayload {
    Raw::HdrMergeSettings settings;
};

struct MaskGeneratorSettings {
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

struct MaskUtilitySettings {
    float blackPoint = 0.0f;
    float whitePoint = 1.0f;
    float gamma = 1.0f;
    float threshold = 0.5f;
    float softness = 0.0f;
    bool invert = false;
};

struct CustomMaskObject {
    int id = 0;
    CustomMaskObjectType type = CustomMaskObjectType::Rectangle;
    CustomMaskOperation operation = CustomMaskOperation::Add;
    std::vector<Vec2> points;
    bool enabled = true;
    bool invert = false;
    float strength = 1.0f;
    float feather = 0.0f;
    float blur = 0.0f;
};

struct CustomMaskPayload {
    int schemaVersion = 1;
    CustomMaskReferenceMode referenceMode = CustomMaskReferenceMode::CustomSize;
    int referenceNodeId = -1;
    std::string referenceSocketId;
    int width = 1024;
    int height = 1024;
    bool aspectLocked = true;
    std::vector<float> rasterLayer;
    std::vector<CustomMaskObject> objects;
    int nextObjectId = 1;
    bool invert = false;
    float blurRadius = 0.0f;
    float expandContract = 0.0f;
    CustomMaskTool activeTool = CustomMaskTool::Brush;
    float brushSize = 48.0f;
    float brushSoftness = 0.45f;
    float brushOpacity = 1.0f;
    bool showCanvasReferenceImage = true;
    bool showCanvasMaskImpact = true;
    bool showCanvasMaskStrength = true;
    int selectedObjectId = -1;
};

struct ImageToMaskSettings {
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

struct ImageGeneratorSettings {
    float colorA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float colorB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float angle = 0.0f;
    float offset = 0.0f;
    std::string text = "Text";
    float fontSize = 96.0f;
};

struct DataMathSettings {
    float constantA = 0.0f;
    float constantB = 1.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float outMin = 0.0f;
    float outMax = 1.0f;
};

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
    RawNeuralDenoisePayload rawNeuralDenoise;
    RawDecodePayload rawDecode;
    RawDevelopPayload rawDevelop;
    RawDetailAutoMaskPayload rawDetailAutoMask;
    RawDetailFusionPayload rawDetailFusion;
    HdrMergePayload hdrMerge;
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

class Graph {
public:
    void Clear();
    void ResetFromLayers(int layerCount, bool hasActiveImage);
    void SyncLayerNodes(int layerCount);

    Node* AddImageNode(ImagePayload payload, Vec2 position);
    Node* AddRawSourceNode(RawSourcePayload payload, Vec2 position);
    Node* AddRawNeuralDenoiseNode(RawNeuralDenoisePayload payload, Vec2 position);
    Node* AddRawDecodeNode(RawDecodePayload payload, Vec2 position);
    Node* AddRawDevelopNode(RawDevelopPayload payload, Vec2 position);
    Node* AddRawDetailAutoMaskNode(RawDetailAutoMaskPayload payload, Vec2 position);
    Node* AddRawDetailFusionNode(RawDetailFusionPayload payload, Vec2 position);
    Node* AddHdrMergeNode(HdrMergePayload payload, Vec2 position);
    Node* AddLutNode(LutPayload payload, Vec2 position);
    Node* AddLayerNode(LayerType type, int layerIndex, Vec2 position);
    Node* AddScopeNode(ScopeKind scopeKind, Vec2 position);
    Node* AddMaskGeneratorNode(MaskGeneratorKind maskKind, Vec2 position);
    Node* AddMaskCombineNode(MaskCombineMode combineMode, Vec2 position);
    Node* AddMaskUtilityNode(MaskUtilityKind utilityKind, Vec2 position);
    Node* AddCustomMaskNode(CustomMaskPayload payload, Vec2 position);
    Node* AddImageToMaskNode(ImageToMaskKind converterKind, Vec2 position);
    Node* AddImageGeneratorNode(ImageGeneratorKind generatorKind, Vec2 position);
    Node* AddMixNode(Vec2 position);
    Node* AddDataMathNode(DataMathMode mode, Vec2 position);
    Node* AddPreviewNode(Vec2 position);
    Node* AddChannelSplitNode(Vec2 position);
    Node* AddChannelCombineNode(Vec2 position);
    Node* AddOutputNode(Vec2 position, bool makePrimary = false);
    Node* AddCompositeNode(Vec2 position);
    Node* EnsureOutputNode();
    void RemoveLayerNode(int layerIndex);

    Node* FindNode(int nodeId);
    const Node* FindNode(int nodeId) const;
    Node* FindNodeByLayerIndex(int layerIndex);
    const Node* FindNodeByLayerIndex(int layerIndex) const;

    std::vector<Node>& GetNodes() { return m_Nodes; }
    const std::vector<Node>& GetNodes() const { return m_Nodes; }
    const std::vector<Link>& GetLinks() const { return m_Links; }
    std::vector<Link>& GetLinks() { return m_Links; }

    NodeGroup* AddGroup(std::string title, Vec2 position, Vec2 size);
    bool RemoveGroup(int groupId);
    NodeGroup* FindGroup(int groupId);
    const NodeGroup* FindGroup(int groupId) const;
    std::vector<NodeGroup>& GetGroups() { return m_Groups; }
    const std::vector<NodeGroup>& GetGroups() const { return m_Groups; }

    void SelectNode(int nodeId, bool additive = false);
    int GetSelectedNodeId() const { return m_SelectedNodeId; }
    bool IsNodeSelected(int nodeId) const;
    void ClearSelection();
    void SetForceOutputFourPins(bool force) { m_ForceOutputFourPins = force; }
    bool IsOutputFourPinsForced() const { return m_ForceOutputFourPins; }
    void SelectNodesInRect(Vec2 min, Vec2 max, bool additive = false);
    void SelectNodesInRect(
        Vec2 min,
        Vec2 max,
        const std::function<Vec2(const Node&)>& sizeResolver,
        bool additive = false);
    const std::vector<int>& GetSelectedNodeIds() const { return m_SelectedNodeIds; }
    void SelectLink(int fromNodeId, int toNodeId);
    void SelectLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    void ClearSelectedLink();
    const Link* GetSelectedLink() const;
    bool HasSelectedLink() const;

    void ConnectImageToOutput(int nodeId);
    void DisconnectOutput();
    bool TryConnect(int fromNodeId, int toNodeId, std::string* errorMessage = nullptr);
    bool CanConnectSockets(
        int fromNodeId,
        const std::string& fromSocketId,
        int toNodeId,
        const std::string& toSocketId,
        std::string* normalizedToSocketId = nullptr,
        std::string* errorMessage = nullptr) const;
    bool TryConnectSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage = nullptr);
    bool SetOutputNodeEnabled(int nodeId, bool enabled);
    bool RemoveNode(int nodeId);
    bool RemoveLink(int fromNodeId, int toNodeId);
    bool RemoveLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    bool RemoveSelectedLink();
    bool HasLink(int fromNodeId, int toNodeId) const;
    bool HasLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const;
    int GetActiveImageNodeId() const { return m_ActiveImageNodeId; }
    void SetActiveImageNodeId(int nodeId) { m_ActiveImageNodeId = nodeId; }
    bool IsOutputConnected() const;
    std::vector<int> GetOutputNodeIds() const;
    std::vector<int> GetConnectedOutputNodeIds() const;
    std::vector<CompletedChainInfo> GetCompletedChains() const;
    std::vector<int> GetDownstreamRenderNodeIds(int nodeId) const;
    std::vector<int> GetDownstreamOutputNodeIds(int nodeId) const;
    int FindAdjacentMainChainNodeId(int nodeId, int direction) const;
    int ResolvePreviewOutputNodeId() const;
    std::uint64_t GetStructureRevision() const { return m_StructureRevision; }

    int GetOutputNodeId() const { return m_OutputNodeId; }
    void SetOutputNodeId(int nodeId) { m_OutputNodeId = nodeId; }
    int GetNextNodeId() const { return m_NextNodeId; }
    void SetNextNodeId(int nextNodeId) { m_NextNodeId = nextNodeId; }
    int GetNextGroupId() const { return m_NextGroupId; }
    void SetNextGroupId(int nextGroupId) { m_NextGroupId = nextGroupId; }
    void RebuildLinks();
    void AutoLayout();
    ValidationResult Validate() const;
    std::vector<int> GetRenderLayerNodePath() const;
    std::vector<int> GetRenderLayerNodePath(int outputNodeId) const;
    std::vector<int> GetRenderLayerIndexPath() const;
    std::vector<int> GetRenderLayerIndexPath(int outputNodeId) const;
    LinkRole GetLinkRole(const Link& link) const;
    bool IsRenderChainNode(const Node& node) const;
    std::vector<SocketDefinition> GetSockets(const Node& node, bool visibleOnly = false) const;
    bool FindSocket(int nodeId, const std::string& socketId, SocketDefinition* outSocket = nullptr) const;
    std::string DefaultInputSocket(const Node& node) const;
    std::string DefaultOutputSocket(const Node& node) const;
    std::string ResolveSocketChannel(int nodeId, const std::string& socketId) const;
    bool IsScalarSocketStream(int nodeId, const std::string& socketId) const;
    int ResolveReferenceSourceNodeId(int nodeId, const std::string& socketId) const;
    int ResolveReferenceSourceNodeIdForOutput(int outputNodeId) const;
    const Link* FindInputLink(int nodeId, const std::string& socketId = kImageInputSocketId) const;
    const Link* FindAnyInputLink(int nodeId, const std::string& socketId) const;
    const Link* FindOutputLink(int nodeId, const std::string& socketId = kImageOutputSocketId) const;
    const Link* FindScopeInputLink(int nodeId) const;
    bool IsRenderLink(const Link& link) const;

private:
    int AllocateNodeId();
    void TouchStructure();
    Vec2 DefaultLayerPosition(int layerIndex) const;
    bool WouldCreateCycle(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const;
    void RemoveRenderLinksForNodeInput(int nodeId, const std::string& socketId);
    void RemoveRenderLinksForNodeOutput(int nodeId, const std::string& socketId);
    void RemoveScopeLinksForNodeInput(int nodeId, const std::string& socketId);
    void RemoveLinksForNodeInput(int nodeId, const std::string& socketId);
    void ActivateImageNode(int nodeId);
    void RefreshPrimaryOutputNode();

    std::vector<Node> m_Nodes;
    std::vector<Link> m_Links;
    std::vector<NodeGroup> m_Groups;
    int m_NextNodeId = 1;
    int m_NextGroupId = 1;
    int m_SelectedNodeId = -1;
    std::vector<int> m_SelectedNodeIds;
    Link m_SelectedLink;
    bool m_HasSelectedLink = false;
    int m_ActiveImageNodeId = -1;
    int m_OutputNodeId = -1;
    bool m_ForceOutputFourPins = false;
    mutable std::uint64_t m_StructureRevision = 1;
    mutable std::uint64_t m_CompletedChainsCacheRevision = 0;
    mutable std::vector<CompletedChainInfo> m_CompletedChainsCache;
};

} // namespace EditorNodeGraph
