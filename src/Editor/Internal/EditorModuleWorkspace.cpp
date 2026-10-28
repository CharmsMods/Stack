#include "Editor/EditorModule.h"
#include "App/WorkspaceInputScope.h"
#include "App/WorkspacePresentation.h"
#include "App/UiPerformanceTrace.h"
#include "App/UiPerformanceProbe.h"
#include "imgui_internal.h"
#include "Notifications/NotificationPresenter.h"

using Stack::Project::WorkspaceMode;

WorkspaceMode EditorModule::GetWorkspaceMode() const { return m_Project->workspace.mode; }
bool EditorModule::IsViewMode() const { return GetWorkspaceMode() == WorkspaceMode::View; }

bool EditorModule::SetWorkspaceMode(WorkspaceMode mode) {
    if (mode != GetWorkspaceMode() && IsBracketingPresentationActive()) return false;
    if (mode == WorkspaceMode::Graph) {
        m_Project->workspace.graphPanelOpen = false;
        m_Sidebar.GetNodeGraphUI().CloseTransientDrawers();
    }
    if (GetWorkspaceMode() == mode) return true;
    if (!FinishWorkspaceInteraction()) return false;
    if (mode != WorkspaceMode::View) m_NodeOpenTransition.Reset();
    CancelCanvasTool();
    m_RawImageInput = {};
    m_RawWorkspaceLabUi.previewSpacePanning = false;
    m_RawWorkspaceLabUi.previewPanning = false;
    m_Project->workspace.mode = mode;
    UpdateNodeEditorPanelVisibility();
    m_RawWorkspaceRootTabActive = mode == WorkspaceMode::View;
    m_RawWorkspaceLockedByEditorProject = false;
    // Switching presentation must not switch the authored graph being edited.
    if (IsRawWorkspaceProjectActive() && !IsRawProjectCanvas())
        OpenRawLayerMaskGraph(m_SelectedRawAdjustmentLayer, false);
    m_Viewport.FrameTransition().Reset();
    MarkDirty();
    MarkRenderRefreshDirty();
    return true;
}

void EditorModule::WorkspaceLoaded(WorkspaceMode mode) {
    m_NodeOpenTransition.Reset();
    m_Project->workspace.Loaded(mode);
    m_RawWorkspaceRootTabActive = mode == WorkspaceMode::View;
    m_RawWorkspaceLockedByEditorProject = false;
    m_Project->graphHistory.Reset(CaptureProjectGraphState());
    m_Project->uniformFeedback.clear();
}

bool EditorModule::WorkspacePanelOpen() const {
    return IsViewMode() ? m_ViewSectionOpen : m_Project->workspace.graphPanelOpen;
}
void EditorModule::SetWorkspacePanelOpen(bool open) {
    if (IsViewMode()) SetViewSectionPanelOpen(open);
    else m_Project->workspace.graphPanelOpen=open;
}

void EditorModule::RenderViewWorkspace() {
    UpdateNodeOpenTransition();
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::ViewWorkspace);
    Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Workspace,2);
    if (Stack::UiPerformance::Probe::Active())
        Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Project,
            Stack::UiPerformance::Probe::Key(GetProjectDocumentId()) ^ reinterpret_cast<std::uintptr_t>(this));
    const auto& io = ImGui::GetIO();
    const bool escapeOwned = ImGui::IsAnyItemActive() || ImGui::IsAnyMouseDown() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
        m_Project->rawLayers.GestureActive() || ImGui::GetIO().KeyAlt;
    // Do not let the second graph click begin an image gesture mid-morph.
    // Keep Escape available below so opening can be interrupted.
    PumpNonRenderingWork(2.5);
    const bool rawProject = IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive();
    // A capture-set draft has Source controls before import creates its RAW
    // project or graph. It belongs to the same View, without graph repair.
    if (rawProject || IsBracketingToolActive()) {
        if (rawProject && !IsRawProjectCanvas()) OpenRawLayerMaskGraph(m_SelectedRawAdjustmentLayer, false);
        Stack::Workspace::InputScope transitionInput(m_NodeOpenTransition.Active(), false);
        RenderRawWorkspaceLabUI();
    } else {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        m_RawWorkspaceLabEditingSurfaceReveal = 1.f;
        RawWorkspaceEditContext context;
        BeginRawWorkspaceEditContext(nullptr, context);
        if (SelectedEditingNode()) RenderRawFloatingControls(context, origin, size, context.canEdit);
        const bool ownsPointer = RawFloatingSurfaceOwnsPointer();
        Stack::Workspace::InputScope pointer(ownsPointer, false);
        Stack::Workspace::InputScope transitionInput(m_NodeOpenTransition.Active(), false);
        m_Viewport.Render(this, 1.f, EditorViewport::HostMode::DockedPane);
        SubmitRenderIfReady();
    }
    if (!ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive()) m_Project->graphHistory.EndGesture();
    if (!Stack::Workspace::IsPreview() && CanConsumeEditorCommandKeys() && io.KeyCtrl && !io.WantTextInput &&
        !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z,false)) {
            if (io.KeyShift) RedoProjectGraphEdit(); else UndoProjectGraphEdit();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y,false)) RedoProjectGraphEdit();
    }
    // Editing widgets already consume Escape for cancellation. A later press
    // leaves View; popups, text entry, drags and the Alt selector take priority.
    if (!escapeOwned && !Stack::Workspace::IsPreview() && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        !io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsAnyMouseDown() && !io.KeyAlt &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !m_Project->rawLayers.GestureActive())
        ReturnToGraphFromNode();
}
