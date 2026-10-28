#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Project/RawLayerStack.h"
#include <imgui.h>

using namespace Stack::Editor;
using Stack::Project::NodeEditTarget;

void EditorModule::ResetNodeEditorPanelInstances(const std::vector<NodeEditTarget>& targets) {
    if (!FinishWorkspaceInteraction()) return;
    const auto previous=m_Project->nodeEditor;
    for (const auto& target:targets) {
        RawWorkspaceEditContext context;
        if (!BeginNodeEditorContext(target,context) || !context.canEdit) continue;
        const auto* node=ResolveNodeEditorTarget(target);
        if (!node) continue;
        const auto reset=FindNodeEditor(*node).reset;
        if (!reset) continue;
        BindNodeEditorPrimaryState(target);
        context.presentation=NodeEditorPresentation::Panel;
        const bool changed=reset(*this,context);
        if (changed) CommitRawWorkspaceEditContext(context,true,true);
        if (!context.error.empty()) {
            PostNotification(UiNotificationSeverity::Error,context.error,"controls-reset");
            break;
        }
        m_NodeEditorState.EndInteraction(target);
    }
    // End the existing adaptive-preview interaction as well as its history
    // group; a one-shot menu action must not leave the image interactive.
    for (const auto& target:targets) if (target.layerId!=Stack::Project::kProjectGraphId) {
        RawWorkspaceEditContext finish;
        if (BeginNodeEditorContext(target,finish)) CommitRawWorkspaceEditContext(finish,false,false);
        break;
    }
    m_Project->rawLayers.EndGesture();m_Project->graphHistory.EndGesture();
    if (previous && ResolveNodeEditorTarget(*previous)) BindNodeEditorPrimaryState(*previous);
}

void EditorModule::RenderNodeEditorPanelMenu() {
    using namespace Stack::Editor::RawLabInternal;
    if (BareToolIslandButton("...",false)) ImGui::OpenPopup("ControlsMenu");
    if (!ImGui::BeginPopup("ControlsMenu")) return;
    RefreshNodeEditorPanel();
    std::vector<NodeEditTarget> resetTargets;
    if (ImGui::BeginMenu("Reset")) {
        for (const auto& group:m_NodeEditorPanel.groups) {
            if (group.instances.empty()) continue;
            const auto* first=ResolveNodeEditorTarget(group.instances.front().target);
            const bool enabled=first && FindNodeEditor(*first).reset;
            ImGui::PushID(group.definitionId.c_str());
            if (group.instances.size()==1) {
                if (ImGui::MenuItem(group.label.c_str(),nullptr,false,enabled))
                    resetTargets.push_back(group.instances.front().target);
            } else if (ImGui::BeginMenu(group.label.c_str(),enabled)) {
                if (ImGui::MenuItem("Reset all"))
                    for (const auto& instance:group.instances) resetTargets.push_back(instance.target);
                ImGui::Separator();
                for (const auto& instance:group.instances) {
                    const auto* node=ResolveNodeEditorTarget(instance.target);
                    const auto* layer=Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),instance.target.layerId);
                    const auto label=instance.ordinal+"  "+(layer ? layer->name : "Project")+" / "+(node ? node->title : "Unavailable");
                    if (ImGui::MenuItem(label.c_str())) resetTargets.push_back(instance.target);
                }
                ImGui::EndMenu();
            }
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
    // Apply after menu enumeration so graph replacement cannot invalidate it.
    if (!resetTargets.empty()) ResetNodeEditorPanelInstances(resetTargets);
}
