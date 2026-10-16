#include "Editor/EditorModule.h"
#include "Notifications/NotificationPresenter.h"
#include "App/WorkspacePresentation.h"
#include <imgui_internal.h>
#include <algorithm>

bool EditorModule::IsRawProjectCanvas() const {
    return IsEditingRawLayerMaskGraph() && m_RawLayerMaskWorkspace->projectCanvas.has_value();
}

bool EditorModule::IsRawImageNavigationActive() const {
    const auto& io=ImGui::GetIO();
    if(m_RawFloatingSurface.input.OwnsPointer())return false;
    using Phase=ImGuiExtras::FloatingSurfaceInput::Phase;
    return m_RawImageInput.phase==Phase::Moving || m_RawImageInput.phase==Phase::Drain ||
        (!io.AppFocusLost && ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
         (m_RawWorkspaceLabUi.previewPanning || m_Viewport.IsPanning())) ||
        m_RawWorkspaceLabUi.previewSpacePanning ||
        (!io.AppFocusLost && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt &&
         !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) &&
         ImGui::IsKeyDown(ImGuiKey_Space) && !m_RawWorkspaceLabUi.zoneAreas.active &&
         m_RawWorkspaceLabUi.gradientDragHandle<0 && !m_RawAssetCapture.drawing &&
         !m_RawWorkspaceLabUi.colorWarpPendingCircleDrawing &&
         !(m_EditingRawLayerMask && m_RawLayerMaskDrag>=0));
}

