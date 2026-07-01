#pragma once

#include "Editor/LoadedProjectData.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawImageData.h"
#include "Renderer/RenderPipeline.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Stack::EditorModuleTypes {

enum class DevelopCandidateFeedbackGateDecision {
    Apply,
    DeferRecentInteraction,
    DropStaleInteraction
};

enum class ViewportMode {
    SingleOutputPreview,
    CompositeCanvas
};

enum class CanvasToolKind {
    None,
    PickColor,
    ToneCurveTarget,
    AdjustAberrationCenter
};

enum class CompositeExportBoundsMode {
    Auto,
    Custom
};

enum class CompositeExportBackgroundMode {
    Transparent,
    Solid
};

enum class CompositeExportAspectPreset {
    Ratio1x1,
    Ratio4x3,
    Ratio3x2,
    Ratio16x9,
    Ratio9x16,
    Ratio2x3,
    Ratio5x4,
    Ratio21x9,
    Custom
};

enum class CompositeSnapModePreset {
    Off,
    ObjectOnly,
    Full,
    Custom
};

enum class CompositeResizeMode {
    Stretch,
    Scale
};

enum class CompositeScaleOriginMode {
    Opposite,
    Center
};

struct CompositeFloatRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct CompositeExportSettings {
    CompositeExportBoundsMode boundsMode = CompositeExportBoundsMode::Auto;
    CompositeExportBackgroundMode backgroundMode = CompositeExportBackgroundMode::Transparent;
    std::array<float, 4> backgroundColor { 0.08f, 0.08f, 0.08f, 1.0f };
    float customX = -512.0f;
    float customY = -512.0f;
    float customWidth = 1024.0f;
    float customHeight = 1024.0f;
    CompositeExportAspectPreset aspectPreset = CompositeExportAspectPreset::Ratio1x1;
    float customAspectRatio = 1.0f;
    int outputWidth = 1024;
    int outputHeight = 1024;
};

struct CompositeSnapSettings {
    bool enabled = false;
    bool snapToObjects = true;
    bool snapToCenters = true;
    bool snapToCanvasCenter = true;
    bool snapToExportBounds = false;
    float rotateSnapStep = 15.0f;
    float scaleSnapStep = 0.1f;
    float lastNonZeroRotateSnapStep = 15.0f;
    float lastNonZeroScaleSnapStep = 0.1f;
};

struct CompositeSceneItem {
    int outputNodeId = -1;
    ImVec2 position = ImVec2(0.0f, 0.0f);
    ImVec2 scale = ImVec2(1.0f, 1.0f);
    float rotation = 0.0f;
    bool visible = true;
    bool locked = false;
    bool placementInitialized = false;
    bool isScalable = false;
    bool keepFullRasterFrame = false;
    std::string label;
    unsigned int texture = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    std::vector<unsigned char> rgbaPixels;
    std::uint64_t cachedRenderRevision = 0;
    std::size_t cachedChainFingerprint = 0;
    std::uint64_t requestedRenderRevision = 0;
    std::size_t requestedChainFingerprint = 0;
    int requestedRasterWidth = 0;
    int requestedRasterHeight = 0;
};

struct GraphPreviewPixels {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    std::uint64_t revision = 0;
};

struct GraphPerformanceStats {
    bool lastInvalidationWasFull = true;
    bool lastSubmissionIncludedMainOutput = false;
    int lastTouchedNodeId = -1;
    int lastDirtyNodeCount = 0;
    int lastDirtyOutputCount = 0;
    int lastSubmittedPreviewCount = 0;
    int lastSubmittedCompositeCount = 0;
    int lastRenderedPreviewCount = 0;
    int lastRenderedCompositeCount = 0;
    bool lastMainOutputTiled = false;
    int lastMainOutputTileCount = 0;
    std::uint64_t lastSubmittedGeneration = 0;
    double lastSnapshotBuildMs = 0.0;
    double lastPreviewRequestBuildMs = 0.0;
    double lastCompositeRequestBuildMs = 0.0;
    double lastMainRenderMs = 0.0;
    double lastPreviewRenderMs = 0.0;
    double lastCompositeRenderMs = 0.0;
    GraphExecutionStats lastMainGraphStats;
};

enum class HdrMergeRenderState {
    Idle,
    Ready,
    Queued,
    Rendering,
    Rendered,
    BlockedMissingInput,
    IncompatibleInput,
    Failed
};

struct HdrMergeInputSummary {
    std::string socketId;
    std::string label;
    std::string sourceLabel;
    std::string metadataSummary;
    std::string normalizationSummary;
    bool active = false;
    bool connected = false;
    bool compatible = true;
    bool hasRawMetadata = false;
    bool hasCaptureExposure = false;
    int sourceNodeId = -1;
    int width = 0;
    int height = 0;
};

struct HdrMergeNodeStatus {
    HdrMergeRenderState state = HdrMergeRenderState::Idle;
    std::string message;
    std::array<HdrMergeInputSummary, 3> inputs {};
    Raw::HdrMergeDebugView debugView = Raw::HdrMergeDebugView::FinalImage;
    bool feedsActiveOutput = false;
    bool hasRenderedResult = false;
    bool stale = false;
    bool metadataNormalizationReady = false;
    bool automaticReliabilityReady = false;
    std::string normalizationMessage;
    std::string reliabilityMessage;
    std::string warningMessage;
};

struct HdrMergeConnectionTopology {
    bool hasInput1 = false;
    bool hasInput2 = false;
    bool hasInput3 = false;
    bool usesInput3 = false;
    bool hasGap = false;
    int activeInputCount = 0;
};

struct PersistedCompositeSceneEntry {
    int outputNodeId = -1;
    ImVec2 position = ImVec2(0.0f, 0.0f);
    ImVec2 scale = ImVec2(1.0f, 1.0f);
    float rotation = 0.0f;
    bool visible = true;
    bool locked = false;
};

struct NodeBrowserThumbnailView {
    const std::vector<unsigned char>* pngBytes = nullptr;
    const std::vector<unsigned char>* decodedPixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::uint64_t revision = 0;
    bool pending = false;
    bool fallback = false;
};

enum class RawAutoValueOwner {
    None,
    AutoBase,
    User
};

struct RawWorkspaceAutoBaseUiState {
    bool hasAppliedViewFit = false;
    bool hasRevertSnapshot = false;
    bool suggestionsOpen = false;
    Stack::RawRecipe::RawDevelopmentRecipe beforeAutoBase;
    RawAutoValueOwner viewTransformOwner = RawAutoValueOwner::None;
    std::uint64_t sourceHash = 0;
    std::uint64_t appliedAnalysisHash = 0;
    std::uint64_t appliedSuggestionSourceHash = 0;
    std::uint64_t appliedSuggestionAnalysisHash = 0;
    std::string appliedSuggestionKey;
    std::string appliedSuggestionLabel;
    std::string appliedSuggestionSection;
    std::string sourceKey;
    std::string summary;
    bool hasMetadataSummary = false;
    Stack::RawAnalysis::RawMetadataSummary metadataSummary;
    Stack::RawAutoBase::AutoBaseRecommendations recommendations;
};

struct RawWorkspaceLayoutUiState {
    float controlsPanelWidth = 420.0f;
    bool diagnosticsOpen = false;
    bool diagnosticsOpenRequested = false;
};

struct DevelopSubjectViewportRegion {
    int id = 0;
    EditorNodeGraph::DevelopSubjectImportanceMode mode = EditorNodeGraph::DevelopSubjectImportanceMode::Important;
    bool enabled = true;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float radiusX = 0.18f;
    float radiusY = 0.18f;
    float feather = 0.35f;
    float strength = 0.75f;
};

struct DevelopSubjectViewportStrokePoint {
    float x = 0.5f;
    float y = 0.5f;
};

struct DevelopSubjectViewportStroke {
    int id = 0;
    EditorNodeGraph::DevelopSubjectImportanceMode mode = EditorNodeGraph::DevelopSubjectImportanceMode::Important;
    bool enabled = true;
    bool subtract = false;
    float radius = 0.045f;
    float feather = 0.35f;
    float strength = 0.75f;
    std::vector<DevelopSubjectViewportStrokePoint> points;
};

struct DevelopSubjectViewportMapCell {
    float importance = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;
    float confidence = 0.0f;
    float boundaryHint = 0.0f;
};

struct DevelopSubjectViewportState {
    int nodeId = 0;
    bool enabled = false;
    bool showOverlay = false;
    float overlayOpacity = 0.45f;
    bool showInterpretedMapOverlay = false;
    bool interpretedMapActive = false;
    float interpretedMapOpacity = 0.32f;
    int interpretedMapGridWidth = 0;
    int interpretedMapGridHeight = 0;
    bool showRefinedMapOverlay = false;
    bool refinedMapActive = false;
    float refinedMapOpacity = 0.36f;
    int refinedMapGridWidth = 0;
    int refinedMapGridHeight = 0;
    bool brushEnabled = false;
    bool brushSubtract = false;
    EditorNodeGraph::DevelopSubjectImportanceMode brushMode =
        EditorNodeGraph::DevelopSubjectImportanceMode::Important;
    float brushRadius = 0.045f;
    float brushFeather = 0.35f;
    float brushStrength = 0.75f;
    int activeRegionId = 0;
    int activeStrokeId = 0;
    std::vector<DevelopSubjectViewportMapCell> interpretedMapCells;
    std::vector<DevelopSubjectViewportMapCell> refinedMapCells;
    std::vector<DevelopSubjectViewportRegion> regions;
    std::vector<DevelopSubjectViewportStroke> strokes;
};

enum class EditorSubWindow {
    NodeGraph = 0,
    ExportSettings = 1,
    ComplexNode = 2,
    Presets = 3
};

struct PendingGraphDropImportRequest {
    std::vector<std::string> paths;
    EditorNodeGraph::Vec2 sourcePosition;
};

struct CachedCompositeChainState {
    EditorNodeGraph::CompletedChainInfo info;
    std::size_t fingerprint = 0;
    std::string label;
};

struct ToneCurveViewportInteractionCache {
    bool probeValid = false;
    int probeSamplingBasis = 0;
    float probeU = 0.0f;
    float probeV = 0.0f;
    std::array<float, 4> probeRgba { 0.0f, 0.0f, 0.0f, 1.0f };
    bool selectionSeedValid = false;
    float selectionSeedU = 0.0f;
    float selectionSeedV = 0.0f;
    float selectionSeedInputX = 0.0f;
    float selectionSeedSceneValue = 0.0f;
    std::array<float, 4> selectionSeedRgba { 0.0f, 0.0f, 0.0f, 1.0f };
    int onImageDragPointIndex = -1;
    float onImageDragAnchorInputX = 0.0f;
    float onImageDragAnchorOutputY = 0.0f;
};

struct DevelopAutoGuidanceDraftState {
    bool editing = false;
    EditorNodeGraph::DevelopAutoGuidance guidance;
};

struct RawDevelopExposureDraftState {
    bool editing = false;
    float exposureStops = 0.0f;
};

struct NodeBrowserThumbnailRuntimeEntry {
    std::string previewSeedHash;
    std::uint32_t previewRecipeVersion = 0;
    std::vector<unsigned char> pngBytes;
    std::vector<unsigned char> decodedPixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::uint64_t revision = 0;
    bool pending = false;
    bool fallback = false;
};

struct NodeBrowserPreviewRequestMeta {
    std::string previewKey;
    std::uint32_t previewRecipeVersion = 0;
};

struct NodeBrowserPreviewSeed {
    enum class Kind {
        None,
        Image,
        Raw
    };

    Kind kind = Kind::None;
    std::string seedHash;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    EditorNodeGraph::RawSourcePayload rawSource;
};

struct GraphAutoFocusState {
    bool trackingActive = false;
    bool animationActive = false;
    int nodeId = -1;
    EditorNodeGraph::Vec2 nodePosition {};
    EditorNodeGraph::Vec2 nodeSize {};
    float startPanX = 0.0f;
    float startPanY = 0.0f;
    float startZoom = 1.0f;
    float targetPanX = 0.0f;
    float targetPanY = 0.0f;
    float targetZoom = 1.0f;
    float lastCanvasWidth = 0.0f;
    float lastCanvasHeight = 0.0f;
    double animationStartTime = 0.0;
};

struct CustomMaskBrushAdjustDrag {
    int nodeId = -1;
    ImVec2 startMouse { 0.0f, 0.0f };
    ImVec2 currentMouse { 0.0f, 0.0f };
    float startSize = 48.0f;
    float startSoftness = 0.45f;
    float startOpacity = 1.0f;
    bool adjusting = false;
};

enum class CompositeEdgeSnapMode {
    None,
    GraphOnly,
    ViewportOnly
};

struct DeferredLoadedProjectApplyState {
    enum class Step {
        None,
        ResetRuntime,
        InstallSource,
        DeserializeLayers,
        FinalizePipeline,
        RestorePersistedThumbnails,
        FinalizeBookkeeping,
        PrepareNodeBrowserThumbnails,
        WaitForFirstRender,
        WaitForNodeBrowserThumbnails,
        Complete,
        Failed
    };

    bool active = false;
    bool failed = false;
    bool allowRenderSubmission = false;
    Step step = Step::None;
    std::shared_ptr<EditorLoadedProjectData> project;
    nlohmann::json layerArray = nlohmann::json::array();
    std::size_t nextLayerIndex = 0;
    std::size_t nextThumbnailIndex = 0;
    std::uint64_t targetRenderRevision = 0;
    std::string statusText;
};

} // namespace Stack::EditorModuleTypes
