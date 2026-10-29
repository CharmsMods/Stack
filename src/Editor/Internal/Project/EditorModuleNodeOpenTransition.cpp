#include "Editor/EditorModule.h"
#include "Notifications/NotificationPresenter.h"
#include "App/NodeTransitionCapture.h"

void EditorModule::BeginNodeOpenTransition(Stack::Editor::NodeOpenTransition::Appearance appearance) {
    if ((!IsViewMode() && !m_OpenRawLabTabRequested) || Stack::Notifications::SystemReducedMotion()) return;
    m_RawImageViewTransition.Reset();
    m_Viewport.FrameTransition().Clear();
    Stack::UiDiagnostics::ArmNodeOpenCapture(GetProjectDocumentId(), appearance.nodeUuid);
    m_NodeOpenTransition.Begin(std::move(appearance), GetProjectDocumentId(), ImGui::GetTime());
}

void EditorModule::UpdateNodeOpenTransition() {
    if (!m_NodeOpenTransition.Active() || m_NodeOpenTransitionFrame == ImGui::GetFrameCount()) return;
    m_NodeOpenTransitionFrame = ImGui::GetFrameCount();
    m_NodeOpenTransition.Tick(GetProjectDocumentId(), ImGui::GetTime(), Stack::Notifications::SystemReducedMotion());
    if (m_NodeOpenTransition.Closed()) {
        SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph);
        m_NodeOpenTransition.Reset();
    }
}

void EditorModule::ReturnToGraphFromNode() {
    if (m_NodeOpenTransition.Closing() || !FinishWorkspaceInteraction()) return;
    // Synchronize the graph projection after edits, then resolve the CURRENT
    // editing target by authored identity (canvas IDs are not persistent).
    if (IsRawWorkspaceProjectActive()) OpenRawLayerMaskGraph(m_SelectedRawAdjustmentLayer, false);
    const auto* selected = SelectedEditingNode();
    const std::string uuid = selected ? selected->instanceUuid : std::string{};
    const std::string owner = m_Project->nodeEditor ? m_Project->nodeEditor->layerId :
        m_SelectedRawAdjustmentLayer.empty() ? Stack::Project::kRawBackgroundId : m_SelectedRawAdjustmentLayer;
    int target = -1;
    for (const auto& node : GetNodeGraph().GetNodes())
        if (!uuid.empty() && node.instanceUuid == uuid &&
            (!IsRawProjectCanvas() || node.rawLayerOwnerId == owner ||
                (IsBracketingToolActive() && node.id==m_RawLayerMaskWorkspace->projectCanvas->preparedSource))) {
            target = node.id; break;
        }
    if (IsRawProjectCanvas() && !m_Project->nodeEditor) {
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Light)
            target = m_RawLayerMaskWorkspace->projectCanvas->preparedSource;
        else if (m_RawWorkspaceLabUi.activeTool == RawLabTool::View)
            target = m_RawLayerMaskWorkspace->projectCanvas->output;
    }
    m_Project->workspace.graphPanelOpen = false;
    auto& canvas = m_Sidebar.GetNodeGraphUI();
    canvas.CloseTransientDrawers();
    if (target > 0 && !Stack::Notifications::SystemReducedMotion() &&
        m_NodeOpenTransition.Close(canvas.CaptureNodeAppearance(this, target,
            m_GraphReturnMinimum, m_GraphReturnMaximum), GetProjectDocumentId(), ImGui::GetTime(),
            m_RawImageBackdrop.frame == ImGui::GetFrameCount() ? m_RawImageBackdrop.extensionOpacity : 0.f)) {
        m_GraphReturnCapturedOrigin = m_GraphReturnMinimum;
        m_NodeOpenTransition.SetReturnOffset({});
        m_Viewport.FrameTransition().Clear();
        m_RawImageViewTransition.Reset();
        return;
    }
    SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph);
}

void EditorModule::SetGraphReturnBounds(ImVec2 minimum, ImVec2 maximum) {
    m_GraphReturnMinimum = minimum;
    m_GraphReturnMaximum = maximum;
    if (m_NodeOpenTransition.Closing())
        m_NodeOpenTransition.SetReturnOffset({minimum.x-m_GraphReturnCapturedOrigin.x,
            minimum.y-m_GraphReturnCapturedOrigin.y});
}

void EditorModule::PresentNodeOpenImage(ImVec2& minimum, ImVec2& maximum) {
    m_NodeOpenTransition.Present(minimum, maximum, ImGui::GetTime());
}

void EditorModule::FinishNodeOpenPhoto(ImDrawList* drawList, int firstVertex) {
    if (!m_NodeOpenTransition.Active()) return;
    m_NodeOpenTransition.FinishPhoto(*drawList, firstVertex, ImGui::GetTime());
    m_NodeOpenTransition.DrawNode(*drawList, ImGui::GetTime());
}

void EditorModule::PublishNodeOpenBackdrop(ImVec2 minimum, ImVec2 maximum,
    ImVec2 viewMinimum, ImVec2 viewMaximum, ImDrawList* drawList, int firstCommand) {
    if (!IsViewMode() || !HasViewImageSurround() ||
        IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) return;
    PublishRawImageBackdrop(minimum, maximum, viewMinimum, viewMaximum, false);
    if (m_RawImageBackdrop.frame == ImGui::GetFrameCount() && !m_RawImageBackdrop.patches.empty()) {
        m_RawImageBackdrop.sharpDrawList=drawList;
        m_RawImageBackdrop.sharpCommandBegin=firstCommand;
        m_RawImageBackdrop.sharpCommandEnd=drawList->CmdBuffer.Size;
    }
}
