#include "Editor/EditorModule.h"
#include "Renderer/GLLoader.h"

void EditorModule::ResetProjectInteractionState() {
    m_PreviewRequestCandidates = {};
    m_PreviewCachePruneGeneration = 0;
    // Node IDs and source identities can be reused by a replacement document.
    // Neither histories nor unfinished gestures may follow them into it.
    m_Project->graphHistory = {};
    m_Project->uniformFeedback.clear();
    m_Project->workspace = {};
    m_ProjectInteractionUi = {};
    m_Project->rawInteractionDraft = {};
    m_Project->nodeEditor.reset();
    m_Project->layerNodeSelection.clear();
    m_Project->rawOperationSelection.clear();
    m_SelectedRawAdjustmentLayer.clear();
    m_RawLayerPanel.Clear();
    m_RawAssetPreviews.Clear();
    m_RawLayerToolPicker = {};
    m_RawOperationRecipeCache = {};
    m_RawOperationInputCache = {};
    m_RawLayerStatus.clear();
    m_RawLayerMaskWorkspace.reset();
    m_GraphEditorUsesRawLayer = false;
    m_EditingRawLayerMask.reset();
    m_RawLayerMaskGenerator = -1;
    m_RawLayerMaskDrag = -1;
    m_MfdExperimentalParameterDraft = {};
    m_MultiFrameWorkspaceGraphGestureDirty = false;
    CancelCanvasTool();
    CancelGraphAutoFocusTracking();
    ClearTrackedToneCurveProbe();
    m_Sidebar.GetNodeGraphUI().ResetProjectInteractionState();

    auto& ui = m_RawWorkspaceLabUi;
    ui.exposureHistory = {};
    if (ui.zoneAreas.overlayTexture) {
        glDeleteTextures(1, &ui.zoneAreas.overlayTexture);
    }
    ui.zoneAreas = {};
    ui.zonesCurveGraph = {};
    ui.toneCurveGraphs = {};
    ui.denoiseMap = {};
    ui.selectedZonePoint = -1;
    ui.selectedTonePoint = -1;
    ui.selectedEvGradient = -1;
    ui.selectedToneGradient = -1;
    ui.hoveredEvGradient = -1;
    ui.hoveredToneGradient = -1;
    ui.gradientDragHandle = -1;
    ui.gradientDrawShape = -1;
    ui.gradientDragOriginal = {};
    ui.lightSurfaceSourceKey.clear();
    ui.lightInteractionActive = false;
    ui.globalExposureInteractionActive = false;
    ui.calibrationEditor = {};
    m_RawFloatingSurface.input = {};
    ui.colorWarpInteractionActive = false;
    ui.colorWarpDraggingSource = false;
    ui.colorWarpQualifierDragHandle = 0;
    ui.colorWarpDraggingProvisional = false;
    ui.colorWarpPendingCircleActive = false;
    ui.colorWarpPendingCircleDrawing = false;
    ui.colorWarpPendingCircleRefining = false;
    ui.colorWarpPendingCircleCommitRequested = false;
    ui.previewPanning = false;
    ui.clearConfirmationRequested = false;
    ui.toneGraphFittedRangeValid = {};
}
