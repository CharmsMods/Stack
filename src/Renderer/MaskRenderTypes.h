#pragma once

#include "Color/LutData.h"
#include "Editor/Layers/LayerBase.h"
#include "MFSR/MFSRTypes.h"
#include "NeuralDenoise/NeuralDenoiseTypes.h"
#include "NodeMath/ContractTypes.h"
#include "NodeMath/OutputInspection.h"
#include "NodeMath/TechnicalImageMath.h"
#include "NodeMath/GeometryMath.h"
#include "Raw/RawDevelopmentRecipe.h"
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
    bool enabled = true;
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
    float textBackdropBlur = 0.0f;
    float textBackdropOpacity = 0.0f;
    float textBackdropPadding = 12.0f;
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
    RawDevelopment,
    RawNeuralDenoise,
    RawDecode,
    RawDevelop,
    RawDetailAutoMask,
    RawDetailFusion,
    HdrMerge,
    Mfsr,
    RawProjectSourceSet,
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
    ConstantChannel,
    CustomMask,
    DataMath,
    TechnicalImage,
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
    Reformat
};

enum class RenderMixBlendMode {
    Normal,
    Average,
    Add,
    Multiply,
    Screen,
    StraightSourceOver,
    PremultipliedSourceOver
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
    Remap,
    ImageAverage
};

struct RenderDataMathSettings {
    float constantA = 0.0f;
    float constantB = 1.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float outMin = 0.0f;
    float outMax = 1.0f;
};

enum class RenderSpectrumViewLut {
    Turbo,
    Viridis,
    Inferno,
    Grayscale
};

enum class RenderFrequencyMaskShape {
    LowPass,
    HighPass,
    BandPass,
    BandStop,
    Notch,
    Gaussian,
    Butterworth
};

enum class RenderSpectrumMathMode {
    Multiply,
    Add,
    Subtract,
    Difference
};

enum class RenderMagnitudePhaseMode {
    Magnitude,
    Phase,
    Recombine
};

enum class RenderSpectrumAnalyzerMode {
    RadialEnergy,
    DominantFrequency
};

enum class RenderFrequencyFilterMode {
    AllPass,
    LowPass,
    HighPass,
    BandPass,
    BandStop,
    NotchReject
};

enum class RenderFrequencyTransitionProfile {
    Smooth,
    Gaussian,
    Butterworth,
    Hard
};

enum class RenderFrequencyEdgePolicy {
    Mirror,
    Wrap,
    ZeroPad
};

enum class RenderSpectrumCombineMode {
    Add,
    Subtract
};

enum class RenderSpectrumViewMode {
    Magnitude,
    Phase,
    Real,
    Imaginary
};

struct RenderFrequencyNotch {
    std::string id;
    float frequency = 0.25f;
    float directionDegrees = 0.0f;
    float width = 0.025f;
};

struct RenderFrequencyResponseSettings {
    RenderFrequencyFilterMode mode = RenderFrequencyFilterMode::AllPass;
    RenderFrequencyTransitionProfile profile = RenderFrequencyTransitionProfile::Smooth;
    float lowCutoff = 0.08f;
    float highCutoff = 0.25f;
    float transitionWidth = 0.025f;
    float butterworthOrder = 2.0f;
    std::vector<RenderFrequencyNotch> notches;
};

struct RenderFrequencyFilterSettings {
    RenderFrequencyResponseSettings localResponse;
    RenderFrequencyEdgePolicy edgePolicy = RenderFrequencyEdgePolicy::Mirror;
    float strength = 1.0f;
};

struct RenderApplyFrequencyResponseSettings {
    float strength = 1.0f;
};

struct RenderCombineSpectraSettings {
    RenderSpectrumCombineMode mode = RenderSpectrumCombineMode::Add;
};

struct RenderFrequencyFftSettings {
    RenderFrequencyEdgePolicy edgePolicy = RenderFrequencyEdgePolicy::Mirror;
};

struct RenderSpectrumViewSettings {
    RenderSpectrumViewMode mode = RenderSpectrumViewMode::Magnitude;
    RenderSpectrumViewLut lut = RenderSpectrumViewLut::Turbo;
    float exposure = 1.0f;
    float gamma = 1.0f;
    bool centerDc = true;
};

struct RenderFrequencyMaskSettings {
    RenderFrequencyMaskShape shape = RenderFrequencyMaskShape::LowPass;
    float cutoff = 0.25f;
    float width = 0.12f;
    float feather = 0.08f;
    float order = 2.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    bool invert = false;
};

struct RenderSpectrumMathSettings {
    float amount = 1.0f;
};

struct RenderMagnitudePhaseSettings {
    float exposure = 1.0f;
    float gamma = 1.0f;
};

struct RenderSpectrumAnalyzerSettings {
    float innerRadius = 0.0f;
    float outerRadius = 0.5f;
    bool excludeDc = true;
};

enum class RenderFrequencyResourceKind {
    Spectrum,
    Magnitude,
    Phase
};

struct RenderFrequencyResource {
    unsigned int texture = 0;
    RenderFrequencyResourceKind kind = RenderFrequencyResourceKind::Spectrum;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int paddedWidth = 0;
    int paddedHeight = 0;
    int paddingOriginX = 0;
    int paddingOriginY = 0;
    RenderFrequencyEdgePolicy edgePolicy = RenderFrequencyEdgePolicy::Mirror;
    std::string sourceRole;
    std::string normalization = "forward-unscaled-inverse-1-over-n";
    std::string precision = "rg32f";
    std::string coordinateConvention = "unshifted-dft-cycles-per-pixel";
    bool hermitian = true;
    bool valid = false;
};

struct RenderSpectrumAnalysis {
    bool valid = false;
    std::array<float, 256> radialPower {};
    float bandPower = 0.0f;
    float peakFrequency = 0.0f;
    float peakDirectionDegrees = 0.0f;
    std::size_t fingerprint = 0;
    std::string error;
};

struct RenderGraphImagePayload {
    SharedPixelBuffer pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    Stack::NodeMath::ValueDescriptor sourceDescriptor;
};

struct RenderGraphRawSourcePayload {
    std::string sourcePath;
    Raw::RawMetadata metadata;
    Raw::RawImageData embeddedRawData;
};

struct RenderGraphRawDevelopmentPayload {
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    // Runtime-only source used by managed multi-frame RAW nodes. Ordinary
    // RAW Development nodes leave this empty and continue to load the
    // recipe source path lazily.
    std::shared_ptr<const Raw::RawImageData> embeddedRawData;
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

struct RenderGraphMfsrPayload {
    Stack::Mfsr::MfsrSettings settings;
    Stack::Mfsr::MfsrDiagnosticsSummary diagnostics;
    Stack::Mfsr::MfsrCacheKey cacheKey;
    bool hasPlaceholderCachedOutput = false;
    std::string placeholderStatus;
    std::string errorMessage;
};

struct RenderGraphRawProjectSourceSetPayload {
    std::string sourceSetId;
    std::string unavailableStatus;
    std::uint64_t inputRevision = 0u;
    std::uint64_t postRecipeRevision = 0u;
    std::uint64_t contentHash = 0u;
    bool resultAvailable = false;
    bool quarantined = false;
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
    std::string definitionId;
    std::string definitionVersion;
    std::string definitionHash;
    RenderGraphNodeKind kind = RenderGraphNodeKind::Image;
    Stack::NodeMath::OutputChannelViewMode outputChannelViewMode =
        Stack::NodeMath::OutputChannelViewMode::Neutral;
    float constantChannelValue = 1.0f;
    RenderGraphImagePayload image;
    RenderGraphRawSourcePayload rawSource;
    RenderGraphRawDevelopmentPayload rawDevelopment;
    RenderGraphRawNeuralDenoisePayload rawNeuralDenoise;
    RenderGraphRawDecodePayload rawDecode;
    RenderGraphRawDevelopPayload rawDevelop;
    RenderGraphRawDetailAutoMaskPayload rawDetailAutoMask;
    RenderGraphRawDetailFusionPayload rawDetailFusion;
    RenderGraphHdrMergePayload hdrMerge;
    RenderGraphMfsrPayload mfsr;
    RenderGraphRawProjectSourceSetPayload rawProjectSourceSet;
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
    Stack::NodeMath::TechnicalImageOperation technicalImageOperation =
        Stack::NodeMath::TechnicalImageOperation::Exposure;
    float technicalExposureValue = 0.0f;
    Stack::NodeMath::ReformatSettings reformatSettings;
    Stack::NodeMath::ValueDescriptor semanticDescriptor;
    std::string semanticDescriptorIdentity;
    RenderFrequencyFilterSettings frequencyFilterSettings;
    RenderFrequencyResponseSettings frequencyResponseSettings;
    RenderFrequencyFftSettings frequencyFftSettings;
    RenderFrequencyFftSettings frequencyIfftSettings;
    RenderSpectrumViewSettings spectrumViewSettings;
    RenderApplyFrequencyResponseSettings applyFrequencyResponseSettings;
    RenderCombineSpectraSettings combineSpectraSettings;
    RenderFrequencyMaskSettings frequencyMaskSettings;
    RenderSpectrumMathMode spectrumMathMode = RenderSpectrumMathMode::Multiply;
    RenderSpectrumMathSettings spectrumMathSettings;
    RenderMagnitudePhaseMode magnitudePhaseMode = RenderMagnitudePhaseMode::Magnitude;
    RenderMagnitudePhaseSettings magnitudePhaseSettings;
    RenderSpectrumAnalyzerMode spectrumAnalyzerMode = RenderSpectrumAnalyzerMode::RadialEnergy;
    RenderSpectrumAnalyzerSettings spectrumAnalyzerSettings;
};

struct RenderGraphLink {
    int fromNodeId = -1;
    std::string fromSocketId;
    int toNodeId = -1;
    std::string toSocketId;
    Stack::NodeMath::ValueDescriptor semanticDescriptor;
    std::string semanticDescriptorIdentity;
};

struct RawLocalRangeTargetPreviewRequest {
    bool enabled = false;
    bool requestConnectedRefinement = false;
    bool provisional = true;
    std::uint64_t generation = 0;
    float sourceU = 0.5f;
    float sourceV = 0.5f;
    float hitRadiusU = 0.0f;
    float hitRadiusV = 0.0f;
    int existingZoneIndex = -1;
    bool interactionEditing = false;
    Stack::RawRecipe::RawLocalRangeTargetZone prospectiveZone;
};

struct RenderGraphSnapshot {
    int outputNodeId = -1;
    std::string outputSocketId;
    bool autoGainMaskPreview = false;
    std::string rawWorkspaceLocalRangeOverlayMode;
    bool rawWorkspaceLocalRangeTargetSampleRequested = false;
    bool executionInspectionEnabled = false;
    float rawWorkspaceLocalRangeTargetSampleU = 0.0f;
    float rawWorkspaceLocalRangeTargetSampleV = 0.0f;
    RawLocalRangeTargetPreviewRequest rawWorkspaceLocalRangeTargetPreview;
    std::vector<RenderGraphNode> nodes;
    std::vector<RenderGraphLink> links;
    Stack::NodeMath::ValueDescriptor outputDescriptor;
    std::string outputDescriptorIdentity;
    std::string semanticFingerprint;
    std::vector<Stack::NodeMath::Diagnostic> semanticDiagnostics;
};
