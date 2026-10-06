#include "Editor/NodeGraph/EditorNodeGraphUI.h"

void EditorNodeGraphUI::ResetProjectInteractionState() {
    m_GraphContext = {};
    m_ConnectionCapabilityCache.clear();
    CloseTransientDrawers();
    StopMiddlePanCapture();
    CancelChannelSplitConfirm();
    ResetPerGraphVisualCaches();
    m_NodeLayoutCache.clear();
    m_LastGraphStructureRevision = 0;
    m_ContextTarget = ContextTarget::Canvas;
    m_ContextNodeId = -1;
    m_ContextLink = {};
    m_DragOutputNodeId = -1;
    m_DragOutputSocketId.clear();
    m_DragInputNodeId = -1;
    m_DragInputSocketId.clear();
    m_DragNodeId = -1;
    m_HoveredInputNodeId = -1;
    m_HoveredInputSocketId.clear();
    m_HoveredOutputNodeId = -1;
    m_HoveredOutputSocketId.clear();
    m_NodeContentActive = false;
    m_NodeContentHovered = false;
    m_BoxSelecting = false;
    m_MouseOwner = GraphMouseOwner::None;
    m_LastNodeControlId = 0;
    m_LastTabDown = false;
    m_NodeBrowserSearchBuffer[0] = '\0';
    m_NodeBrowserScrollY = 0.0f;
    m_NodeBrowserRestoreScroll = true;
    m_PushedSourceNodeId = -1;
    m_PushedNodeIds.clear();
    m_EditingGroupId = -1;
    m_GroupRenameBuffer[0] = '\0';
    m_DragGroupId = -1;
    m_ResizingGroupId = -1;
    m_HoveredGroupId = -1;
    m_OpenRenameProjectPopup = false;
    m_RenameProjectBuffer[0] = '\0';
    m_RenamePresetBuffer[0] = '\0';
    m_OpenSavePresetPopup = false;
    m_SavePresetNameBuffer[0] = '\0';
}
