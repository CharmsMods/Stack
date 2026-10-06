#include "Editor/EditorModule.h"
#include "Renderer/GLLoader.h"

void EditorModule::ResetProjectInteractionState() {
    // Node IDs and source identities can be reused by a replacement document.
    // Neither histories nor unfinished gestures may follow them into it.
    m_FrequencyGraphUndo.reset();
    m_FrequencyGraphRedo.reset();
    m_CustomMaskUndoStacks.clear();
    m_CustomMaskRedoStacks.clear();
    m_CustomMaskPaintingNodes.clear();
    m_CustomMaskBrushAdjustDrag = {};
    m_ProjectInteractionUi = {};
    m_Project->rawInteractionDraft = {};
    m_SelectedRawAdjustmentLayer.clear();
    m_RawLayerPanel.Clear();
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
    ui.calibrationGestureActive = false;
    ui.calibrationGestureStart = {};
    ui.calibrationGestureSource.clear();
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
