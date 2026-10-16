#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"

void EditorNodeGraphUI::OpenMaskTextureInspector(const EditorNodeGraph::Node& node) {
    m_MaskTextureInspectorUuid=node.instanceUuid;
    m_MaskTextureInspectorOwner=node.rawLayerOwnerId;
    m_OpenMaskTextureInspector=true;
}

void EditorNodeGraphUI::RenderMaskTextureInspector(EditorModule* editor) {
    ImGui::PushID(this);
    if (m_OpenMaskTextureInspector) {
        ImGui::OpenPopup("Mask texture");
        m_OpenMaskTextureInspector=false;
    }
    ImGui::SetNextWindowSizeConstraints({280,0},{380,720});
    if (ImGui::BeginPopup("Mask texture")) {
        int id=0;
        for (const auto& node:editor->GetNodeGraph().GetNodes())
            if (node.instanceUuid==m_MaskTextureInspectorUuid && node.rawLayerOwnerId==m_MaskTextureInspectorOwner)
                {id=node.id;break;}
        if (!id) ImGui::CloseCurrentPopup();
        else {
            const auto* node=editor->GetNodeGraph().FindNode(id);
            ImGui::TextUnformatted(node->title.c_str());
            ImGui::Separator();
            RenderOperationControls(editor,id);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
