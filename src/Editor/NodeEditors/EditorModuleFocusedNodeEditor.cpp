#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include <imgui_internal.h>

bool EditorModule::IsFocusedNodeEditor() const { return m_NodeEditorPanel.focusTarget.has_value(); }

bool EditorModule::EnterFocusedNodeEditor(const Stack::Project::NodeEditTarget& target) {
    if(IsFocusedNodeEditor() || !IsViewMode() || Stack::Workspace::IsPreview() ||
        ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return false;
    const auto* node=ResolveNodeEditorTarget(target);
    if(!node || !Stack::Editor::FindNodeEditor(*node).panelPrimary || !FinishWorkspaceInteraction())return false;
    if(!ActivateNodeEditorPanelTarget(target))return false;
    const auto* selected=SelectedNodeEditor();
    if(selected && selected->legacyTool==RawLabTool::Zones) {
        m_RawWorkspaceLabUi.zonesTargetedView=false;m_RawWorkspaceLabUi.selectedEvGradient=-1;
    }
    if(selected && selected->legacyTool==RawLabTool::Color) {
        RawWorkspaceEditContext context;
        if(BeginNodeEditorContext(target,context)) {
            const int family=m_NodeEditorState.Get<Stack::Editor::PanelControlState>(target).selectedFamily;
            for(std::size_t i=0;i<context.recipe.colorWarp.pins.size();++i)
                if(context.recipe.colorWarp.pins[i].family==family)m_RawWorkspaceLabUi.selectedColorWarpPin=static_cast<int>(i);
        }
    }
    m_NodeEditorPanel.focusTarget=target;
    m_NodeEditorPanel.focusEnteredFrame=ImGui::GetFrameCount();
    UpdateNodeEditorPanelVisibility();
    return true;
}

void EditorModule::ExitFocusedNodeEditor() {
    if(!IsFocusedNodeEditor())return;
    const auto target=*m_NodeEditorPanel.focusTarget;
    FinishWorkspaceInteraction();
    m_NodeEditorState.EndInteraction(target);
    m_NodeEditorPanel.focusTarget.reset();
    m_NodeEditorPanel.focusEnteredFrame=-1;
    m_RawFloatingSurface.input={};m_RawFloatingSurface.frame=-2;
    UpdateNodeEditorPanelVisibility();
}

void EditorModule::UpdateFocusedNodeEditor() {
    if(!IsFocusedNodeEditor())return;
    if(!IsViewMode() || m_RawWorkspaceLabUi.galleryWorkspaceOpen ||
        m_Project->nodeEditor!=m_NodeEditorPanel.focusTarget) {
        ExitFocusedNodeEditor();return;
    }
    RefreshNodeEditorPanel();
}

void EditorModule::PresentSelectedNodeEditor() {
    if(IsFocusedNodeEditor())ExitFocusedNodeEditor();
    const auto* definition=SelectedNodeEditor();
    if(definition && definition->panelPrimary) {
        m_ViewSectionTab=1;m_ViewSectionOpen=true;m_Project->workspace.viewPanelOpen=true;
        m_RawWorkspaceLabUi.settingsTabActive=false;m_RawAssetCapture.assetsTab=false;
    } else if(definition) {
        m_ViewSectionTab=2;m_RawWorkspaceLabUi.settingsTabActive=true;m_RawAssetCapture.assetsTab=false;
    }
    UpdateNodeEditorPanelVisibility();
}
