#pragma once
#include "Editor/Internal/RawLab/DetailContrastEditor.h"
#include "Editor/GraphScopeData.h"

#include "Editor/LoadedProjectData.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/RawWorkspaceAutoBaseState.h"
#include "Editor/RawVariablePrecisionValueGraph.h"
#include "Editor/RawZoneAreaHistory.h"
#include "Editor/RawExposureHistory.h"
#include "Editor/RawZoneAreaPreview.h"
#include "Editor/Internal/RawLab/RawDenoiseMapEditor.h"
#include "Editor/Internal/RawLab/RawLabColorWheelRenderer.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "Raw/RawColorCloudPacket.h"
#include "Raw/RawColorWarpAnalysis.h"
#include "Raw/RawImageData.h"
#include "Renderer/RenderPipeline.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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
    std::shared_ptr<const GraphScopeData> scopeData;
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
    bool lastMainRegionPlanAvailable = false;
    bool lastMainRegionPlanTileable = false;
    int lastMainRegionPlanHaloX = 0;
    int lastMainRegionPlanHaloY = 0;
    std::string lastMainRegionPlanReason;
    std::uint64_t lastSubmittedGeneration = 0;
    double lastSnapshotBuildMs = 0.0;
    double lastPreviewRequestBuildMs = 0.0;
    double lastCompositeRequestBuildMs = 0.0;
    double lastMainRenderMs = 0.0;
    double lastMainGraphExecuteMs = 0.0;
    double lastMainPostExecuteMs = 0.0;
    double lastPreviewRenderMs = 0.0;
    double lastCompositeRenderMs = 0.0;
    double lastProjectSaveSnapshotMs = 0.0;
    double lastProjectSaveCommitMs = 0.0;
    bool lastRawWorkspaceRender = false;
    bool lastRawInteractivePreview = false;
    bool lastRawAnalysisCaptured = false;
    int lastRawPreviewMaxDimension = 0;
    std::string lastRawRenderPurpose;
    double lastRawQueueWaitMs = 0.0;
    double lastRawWorkerTotalMs = 0.0;
    double lastRawUiAdoptionMs = 0.0;
    double rawAdaptiveFrameTimeMs = 0.0;
    float rawAdaptivePreviewScale = 1.0f;
    std::size_t lastRawSourceTransferredBytes = 0;
    std::size_t lastRawPublishedTextureBytes = 0;
    std::size_t lastRawReadbackTransferredBytes = 0;
    std::size_t lastRawFullFrameEstimatedWorkingSetBytes = 0;
    bool lastRawWorkerStarted = false;
    bool lastRawFullFrameRefinementRequested = false;
    bool lastRawFullFrameRefinementBudgetAllowed = false;
    bool lastRawSuperseded = false;
    std::uint64_t rawVramWorkingBudgetBytes = 0;
    std::uint64_t rawVramAvailableBytes = 0;
    bool rawMinimumMemoryTiling = false;
    double lastSliceImportDecodeMs = 0.0;
    double lastSliceImportQueueMs = 0.0;
    double lastSliceImportPreviewMs = 0.0;
    double lastSliceImportStorageCopyMs = 0.0;
    double lastSliceImportEmbedMs = 0.0;
    int lastSliceImportWidth = 0;
    int lastSliceImportHeight = 0;
    std::size_t lastSliceImportPixelBytes = 0;
    std::size_t lastSliceImportEmbeddedBytes = 0;
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

struct RawWorkspaceLayoutUiState {
    float controlsPanelWidth = 420.0f;
    bool diagnosticsOpen = false;
    bool diagnosticsOpenRequested = false;
    bool dockLayoutInitialized = false;
};

enum class RawLabTool {
    Light = 0,
    Zones = 1,
    Tone = 2,
    View = 3,
    Inspect = 4,
    Transform = 5,
    // Kept at the end so persisted values for the original six tools do not
    // change. Drawer placement is independent of these saved values.
    Denoise = 6,
    // Post-demosaic, scene-linear RGB denoise. Kept after CFA Denoise so all
    // previously persisted tool values retain their meaning.
    RgbDenoise = 7,
    // Project-level organization only. Processing remains intentionally
    // unavailable during the multi-source state/storage foundation.
    MultiFrame = 8,
    // Added after all existing persisted values. Color Warp stays distinct
    // from the project-level MultiFrame surface and Calibration.
    Color = 9,
    Calibration = 10,
    Detail = 11,
    Exposure = 12
};

// Drawer groups keep the concrete tools and their persisted identities intact.
inline constexpr RawLabTool RawLabDrawerTool(RawLabTool tool) {
    switch (tool) {
        case RawLabTool::RgbDenoise: return RawLabTool::Denoise;
        case RawLabTool::Exposure: case RawLabTool::Tone: case RawLabTool::Detail: return RawLabTool::Zones;
        case RawLabTool::Calibration: return RawLabTool::Color;
        default: return tool;
    }
}

inline constexpr std::array<RawLabTool, 6> kRawLabDrawerTools {
    RawLabTool::Transform, RawLabTool::Denoise, RawLabTool::Color,
    RawLabTool::Light, RawLabTool::Zones, RawLabTool::View
};
inline constexpr std::array<const char*, 6> kRawLabDrawerLabels {
    "Transform", "Denoise", "Color", "Exposure", "Curves", "View Transform"
};
inline constexpr int kRawLabBracketingIndex = static_cast<int>(kRawLabDrawerTools.size());

enum class RawGalleryHost {
    Closed = 0,
    Filmstrip = 1,
    NativeWindow = 3
};

enum class RawGalleryNavigationMode {
    ProjectRoot = 0,
    ProjectFrames = 1,
    MultiFrameCreation = 2
};

using RawWorkspaceColorCloudPoint = Raw::RawColorCloudSample;

struct RawWorkspaceColorWarpAreaAnalysisState {
    std::mutex mutex;
    std::uint64_t generation = 0;
    bool ready = false;
    Stack::RawRecipe::RawColorWarpAreaAnalysisResult result;
};

struct RawWorkspaceColorWarpDiagnosticState {
    std::mutex mutex;
    std::uint64_t generation = 0;
    std::size_t fingerprint = 0;
    int width = 0;
    int height = 0;
    bool ready = false;
    bool spatialAvailable = false;
    std::vector<unsigned char> pixels;
};

struct RawWorkspaceColorWarpSmartSelectionState {
    bool active = false;
    std::string pinId;
    float leftEv = 0.0f;
    float rightEv = 0.0f;
    float centerEv = 0.0f;
    std::vector<float> background;
    std::vector<float> residual;
    std::vector<float> profile;
    float amplitude = 0.0f;
};

struct RawCurveGraphUiState {
    bool draggingExposure = false;
    float exposureStart = 0.0f;
    float exposureMouseStartY = 0.0f;
    int selectedSegment = -1;
    int selectedSide = -1; // 0 = left endpoint, 1 = right endpoint.
    bool chooserOpen = false;
    int chooserHotSide = 0;
    int draggingPoint = -1;
    int draggingSegment = -1;
    int draggingSide = -1;
    ImVec2 dragMouseStart { 0.0f, 0.0f };
    ImVec2 dragPointStart { 0.0f, 0.0f };
};

struct RawZoneAreaGraphState {
    RawCurveGraphUiState interaction;
    int selectedPoint = -1;
    float minimumEv = -8.0f;
    float maximumEv = 6.0f;
    float minimumGain = -4.0f;
    float maximumGain = 4.0f;
    std::size_t fittedMask = 0;
    bool fittedFullResolution = false;
};

struct RawZoneAreaUiState {
    Stack::Editor::RawZoneAreaHistory history;
    std::string sourceKey;
    std::string selectedId;
    int mode = 0; // Adjust, Add, Erase.
    float radius = 0.06f;
    float softness = 1.0f;
    float opacity = 1.0f;
    bool followEdges = false;
    float edgeSensitivity = .65f;
    bool showMask = true;
    bool showCursor = true;
    bool effectPreview = false;
    bool legacy = false;
    bool active = false;
    bool changed = false;
    float pressY = 0.0f;
    float pressOffsetEv = 0.0f;
    int gestureMode = 0;
    std::string gestureAreaId;
    Stack::RawRecipe::RawDevelopmentRecipe beforeGesture;
    std::unordered_map<std::string, RawZoneAreaGraphState> graphs;
    std::size_t overlayKey = 0;
    unsigned int overlayTexture = 0;
    int overlayWidth = 0, overlayHeight = 0;
    std::shared_ptr<const Stack::RawRecipe::ImageGuide> neutralGuide;
    std::shared_ptr<Stack::Editor::RawZoneAreaPreview> maskWorker;
    bool maskPending = false;
    struct MaskCache {
        std::size_t key = 0;
        bool fullResolution = false;
        bool authoritative = false, provisional = false;
        std::shared_ptr<const Stack::RawRecipe::ImageGuide> guide;
        std::shared_ptr<const Stack::RawRecipe::RawZoneAreaMaskPreview> preview;
    };
    std::unordered_map<std::string, MaskCache> masks;
};

struct RawPreviewLayoutState {
    bool valid = false;
    bool fitting = true;
    bool actualPixels = false;
    ImVec2 viewportMinimum{}, viewportMaximum{};
    ImVec2 baseImageSize{}, sourceSize{};
    ImVec2 imageMinimum{}, imageSize{};
};

struct RawWorkspaceLabUiState {
    RawLabTool activeTool = RawLabTool::Light;
    RawLabTool lastCurvesTool = RawLabTool::Zones;
    RawLabTool lastColorTool = RawLabTool::Color;
    bool galleryWorkspaceOpen = false;
    bool galleryWorkspaceGrid = false;
    bool galleryPropertiesOpen = true;
    RawGalleryHost galleryHost = RawGalleryHost::Closed;
    RawGalleryHost lastGalleryHost = RawGalleryHost::Filmstrip;
    RawGalleryNavigationMode galleryNavigationMode =
        RawGalleryNavigationMode::ProjectRoot;
    std::string galleryProjectId;
    float toolRailWidth = 340.0f;
    bool toolRailWidthUserAdjusted = false;
    bool toolRailOnRight = false;
    float lowerShelfHeight = 180.0f;
    float filmstripHeight = 132.0f;
    float galleryThumbnailScale = 1.0f;
    bool lowerShelfOpen = false;
    bool gradingScopesShowInput = false;
    bool previewActualPixels = false;
    float previewZoom = 1.0f;
    float previewZoomTarget = 1.0f;
    bool previewZoomAnimating = false;
    ImVec2 previewZoomFocusScreen { 0.0f, 0.0f };
    ImVec2 previewZoomFocusUv { 0.5f, 0.5f };
    float previewPanX = 0.0f;
    float previewPanY = 0.0f;
    bool previewPanning = false;
    double previewSurroundStarted = -1.0;
    float previewSurroundFrom = 0.f;
    bool previewSurroundTarget = false;
    std::string previewViewSourceKey;
    RawPreviewLayoutState previewLayout;
    // Presentation-only RAW section selection: Layers or active-tool Settings.
    bool settingsTabActive = false;
    bool clearConfirmationRequested = false;
    // Exposure owns a large direct-manipulation value surface, a continuous
    // radial demosaic chooser, and an animated Camera/Manual WB selector.
    // These values are presentation-only; the recipe remains the sole saved
    // authority and interaction drafts remain the sole live-edit authority.
    std::string lightSurfaceSourceKey;
    bool lightInteractionActive = false;
    bool calibrationGestureActive = false;
    RawRecipe::RawColorCalibrationRecipe calibrationGestureStart;
    std::string calibrationGestureSource;
    bool globalExposureInteractionActive = false;
    Editor::RawExposureHistory exposureHistory;
    float lightExposureGlow = 0.0f;
    bool lightExposureGraphInitialized = false;
    Stack::Editor::VariablePrecisionValueGraph::State lightExposureGraph;
    std::array<float, 3> lightWhiteBalanceGlow { 0.0f, 0.0f, 0.0f };
    float lightWhiteBalanceModeMix = 0.0f;
    float lightWhiteBalanceFieldsOpacity = 0.0f;
    bool lightDemosaicInitialized = false;
    float lightDemosaicRotation = 0.0f;
    float lightDemosaicTargetRotation = 0.0f;
    double lightDemosaicLastInputTime = -1.0;
    double lightDemosaicLastAcceptedScrollTime = -1.0;
    int lightDemosaicLastAcceptedScrollDirection = 0;
    int lightDemosaicIgnoredOppositeDirection = 0;
    bool lightDemosaicDragging = false;
    ImVec2 lightDemosaicPressPosition { 0.0f, 0.0f };
    float lightDemosaicPreviousPointerAngle = 0.0f;
    int lightDemosaicPressedItem = -1;
    int lightDemosaicSelectedItem = 2;
    bool zonesTargetedView = false;
    RawZoneAreaUiState zoneAreas;
    int selectedZonePoint = -1;
    int selectedEvGradient = -1;
    int selectedToneGradient = -1;
    int hoveredEvGradient = -1;
    int hoveredToneGradient = -1;
    bool gradientOverlayVisible = true;
    bool gradientGuidesVisible = true;
    int gradientDrawShape = -1; // -1 selects, 0 draws linear, 1 draws radial.
    int gradientDragHandle = -1;
    ImVec2 gradientDragStart {0.0f, 0.0f};
    Stack::RawRecipe::RawGradientMask gradientDragOriginal;
    int activePointCurve = 0;
    Stack::Editor::RawLabInternal::DetailContrastEditorState detailContrastEditor;
    int sceneToneView = 0; // Curve, Contrast, creative RGB. Presentation only.
    int sceneToneSelectedPoint = -1;
    int sceneToneDraggingPoint = -1;
    float sceneToneViewMin = -16, sceneToneViewMax = 16;
    // Display-only Tone graph zoom. A value of one fits the graph to the
    // histogram; zero restores the complete authored curve range.
    float toneGraphZoom = 0.0f;
    // Keep the data-derived fitted bounds while a fresh scope readback is in
    // flight. The display can then retain its coordinate system instead of
    // jumping to the full authored range between frames.
    std::array<float, 4> toneGraphFittedMinimum { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> toneGraphFittedMaximum { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<bool, 4> toneGraphFittedRangeValid {};
    int selectedTonePoint = -1;
    RawCurveGraphUiState zonesCurveGraph;
    std::array<RawCurveGraphUiState, 4> toneCurveGraphs;
    Stack::Editor::RawLabInternal::RawDenoiseMapEditorState denoiseMap;
    int selectedColorWarpPin = -1;
    std::string selectedColorWarpGroupId;
    int colorWarpGroupOperation = 0;
    bool colorWarpDraggingSource = false;
    int colorWarpQualifierDragHandle = 0;
    bool colorWarpDraggingProvisional = false;
    bool colorWarpProvisionalPinValid = false;
    bool colorWarpProvisionalCreatedFromPhoto = false;
    float colorWarpProvisionalA = 0.0f;
    float colorWarpProvisionalB = 0.0f;
    bool colorWarpProvisionalSceneEvValid = false;
    float colorWarpProvisionalSceneEv = 0.0f;
    bool colorWarpPhotoHoverValid = false;
    float colorWarpPhotoHoverA = 0.0f;
    float colorWarpPhotoHoverB = 0.0f;
    float colorWarpPhotoHoverSceneEv = 0.0f;
    bool colorWarpLiveCloud = false;
    bool colorWarpInteractionActive = false;
    ImVec2 colorWarpPanAnchorScreenPos { 0.0f, 0.0f };
    ImVec2 colorWarpPanRestoreScreenPos { 0.0f, 0.0f };
    bool colorWarpPendingCircleActive = false;
    bool colorWarpPendingCircleDrawing = false;
    bool colorWarpPendingCircleRefining = false;
    bool colorWarpPendingCircleCommitRequested = false;
    int colorWarpPendingCircleEditKind = 0; // 1 move, 2 resize.
    ImVec2 colorWarpPendingCirclePressPosition { 0.0f, 0.0f };
    float colorWarpPendingCirclePressCenterU = 0.5f;
    float colorWarpPendingCirclePressCenterV = 0.5f;
    float colorWarpPendingCirclePressRadiusU = 0.02f;
    float colorWarpPendingCirclePressRadiusV = 0.02f;
    Stack::RawRecipe::RawColorWarpSampleCircle colorWarpPendingCircle;
    std::uint64_t colorWarpAreaAnalysisGeneration = 0;
    std::shared_ptr<RawWorkspaceColorWarpAreaAnalysisState>
        colorWarpAreaAnalysisState;
    Stack::RawRecipe::RawColorWarpAreaAnalysisResult colorWarpAreaAnalysisResult;
    std::string colorWarpAreaStatus;
    std::string colorWarpPendingReplaceRegionId;
    std::string colorWarpPendingReplaceCircleId;
    bool colorWarpPendingCircleAppend = false;
    bool colorWarpDiagnosticLocked = false;
    bool colorWarpInspectionActive = false;
    bool colorWarpDiagnosticIsolateSelected = false;
    int colorWarpHoveredSelection = -1;
    int colorWarpTemporaryViewMode = 0;
    std::unordered_map<std::string, int> colorWarpSelectionViewModes;
    std::string colorWarpInspectedRegionId;
    float colorWarpRegionReachWheelDelta = 0.0f;
    float colorWarpRegionFeatherWheelDelta = 0.0f;
    int colorWarpDiagnosticTargetLongEdge = 192;
    std::uint64_t colorWarpDiagnosticGeneration = 0;
    std::size_t colorWarpDiagnosticRequestedFingerprint = 0;
    std::shared_ptr<RawWorkspaceColorWarpDiagnosticState>
        colorWarpDiagnosticState;
    // Captured on pointer-down so pin, qualifier, pan, and zoom gestures
    // cannot steal one another until every pressed pointer is released.
    int colorWarpGestureOwner = 0;
    int colorWarpWorkingRange = 0;
    int colorWarpEvEditMode = 0; // 0 Draw, 1 Smart.
    float colorWarpEvBrushWidth = 2.5f;
    int colorWarpEvGesture = 0;
    std::string colorWarpEvGesturePinId;
    ImVec2 colorWarpEvGesturePress { 0.0f, 0.0f };
    ImVec2 colorWarpEvGestureLast { 0.0f, 0.0f };
    Stack::RawRecipe::RawColorWarpEvCurve colorWarpEvGestureStartCurve;
    RawWorkspaceColorWarpSmartSelectionState colorWarpSmartSelection;
    RawWorkspaceColorWarpSmartSelectionState colorWarpSmartHoverSelection;
    RawWorkspaceColorWarpSmartSelectionState colorWarpSmartGestureStart;
    bool colorWarpLightnessRangePreviewActive = false;
    float colorWarpViewCenterA = 0.0f;
    float colorWarpViewCenterB = 0.0f;
    float colorWarpViewExtent = 0.45f;
    float colorWarpViewTargetCenterA = 0.0f;
    float colorWarpViewTargetCenterB = 0.0f;
    float colorWarpViewTargetExtent = 0.45f;
    bool colorWarpViewAnimationInitialized = false;
    bool colorWarpViewNeedsFit = true;
    std::string colorWarpViewSourceKey;
    std::string colorWarpCloudSourceKey;
    std::size_t colorWarpCloudInputFingerprint = 0;
    int colorWarpCloudWorkingSpace = -1;
    std::vector<RawWorkspaceColorCloudPoint> colorWarpCloud;
    unsigned int colorWarpAffectedOverlayTexture = 0;
    int colorWarpAffectedOverlayWidth = 0;
    int colorWarpAffectedOverlayHeight = 0;
    std::size_t colorWarpAffectedOverlayFingerprint = 0;
    std::string colorWarpAffectedOverlaySourceKey;
    Stack::Editor::RawLabInternal::RawLabColorWheelRenderer colorWarpWheelRenderer;
    // The bounded cloud packet is uploaded to this persistent point-sprite
    // buffer.  A single in-order ImGui GPU callback draws it directly over
    // the disc, so ordinary pan/zoom does not allocate GPU objects or expand
    // the command list per sample.
    unsigned int colorWarpCloudProgram = 0;
    unsigned int colorWarpCloudVertexArray = 0;
    unsigned int colorWarpCloudVertexBuffer = 0;
    std::size_t colorWarpCloudGpuFingerprint = 0;
    int colorWarpCloudGpuPointCount = 0;
    float colorWarpCloudCanvasMinX = 0.0f;
    float colorWarpCloudCanvasMinY = 0.0f;
    float colorWarpCloudCanvasWidth = 0.0f;
    float colorWarpCloudCanvasHeight = 0.0f;
    float colorWarpCloudDisplayPosX = 0.0f;
    float colorWarpCloudDisplayPosY = 0.0f;
    float colorWarpCloudDisplayWidth = 0.0f;
    float colorWarpCloudDisplayHeight = 0.0f;
    float colorWarpCloudFramebufferScaleX = 1.0f;
    float colorWarpCloudFramebufferScaleY = 1.0f;
    float colorWarpCloudViewCenterA = 0.0f;
    float colorWarpCloudViewCenterB = 0.0f;
    float colorWarpCloudViewHalfWidth = 1.0f;
    float colorWarpCloudViewHalfHeight = 1.0f;
    float colorWarpCloudPointScale = 1.0f;
    int colorWarpCloudWorkingRange = 0;
    bool colorWarpCloudShowSelectedCatch = false;
    std::string multiFrameStatusText;
};

struct TimelineUiState {
    bool open = false;
    float targetHeight = 220.0f;
    float currentHeight = 0.0f;
    int selectedOutputNodeId = -1;
    Stack::Timeline::AnimatableParameterTarget selectedParameterTarget;
    int currentFrame = 0;
    int durationFrames = 120;
    int framesPerSecond = 30;
    bool playing = false;
    bool loopPlayback = true;
    bool settingsPopupOpen = false;
    double playbackFrameAccumulator = 0.0;
    int liveEditPreviewFrame = -1;
    std::vector<Stack::Timeline::AnimatableParameterTarget> liveEditPreviewTargets;
    std::vector<int> collapsedChainOutputNodeIds;
    std::vector<int> collapsedNodeIds;
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
    std::shared_ptr<EditorLoadedProjectData> rollbackProject;
    bool rollbackDirty = false;
    bool rollbackGalleryOpen = false;
    nlohmann::json layerArray = nlohmann::json::array();
    std::size_t nextLayerIndex = 0;
    std::size_t nextThumbnailIndex = 0;
    std::uint64_t targetRenderRevision = 0;
    std::uint64_t acceptedRenderGenerationAtStart = 0;
    std::string statusText;
    std::function<void(bool, const std::string&)> completion;
};

} // namespace Stack::EditorModuleTypes
