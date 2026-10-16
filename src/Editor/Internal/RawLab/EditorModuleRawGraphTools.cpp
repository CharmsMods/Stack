#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Editor/RawLayerTools.h"
#include "imgui_internal.h"

void EditorModule::RefreshRawLayerToolPicker() const {
    const auto document = GetProjectDocumentId();
    const bool projectGraph = m_Project->nodeEditor && m_Project->nodeEditor->layerId == Stack::Project::kProjectGraphId &&
        !IsBracketingToolActive();
    const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    const auto owner = projectGraph ? std::string(Stack::Project::kProjectGraphId) : layer ? layer->id : std::string{};
    const auto revision = projectGraph ? m_Project->editRevision : m_Project->rawLayers.Revision();
    auto& cache = m_RawLayerToolPicker;
    const auto sourceRevision=m_Project->editRevision;
    if (cache.documentId == document && cache.layerId == owner && cache.revision == revision &&
        cache.sourceRevision==sourceRevision) return;
    cache.documentId = document; cache.layerId = owner; cache.revision = revision;
    cache.sourceRevision=sourceRevision;cache.sourceUuid.clear();
    cache.operationUuids.clear(); cache.labels.clear();
    if (!projectGraph && !layer) return;
    if (!projectGraph && IsBracketingActive()) {
        const auto* source=m_Project->graph.FindNode(ResolveRawWorkspaceStageOutputNodeId());
        if (source && source->kind==EditorNodeGraph::NodeKind::MultiFrameHdr && source->multiFrameHdr.bracketingSource) {
            cache.sourceUuid=source->instanceUuid;
            cache.operationUuids.push_back(cache.sourceUuid);
            cache.labels.push_back(source->title);
        }
    }
    const auto tools = Stack::Editor::RawLayerToolNodes(projectGraph ? m_Project->graph : layer->graph);
    const auto labels=Stack::Editor::RawLayerToolLabels(tools);
    cache.labels.insert(cache.labels.end(),labels.begin(),labels.end());
    for (const auto* node : tools) cache.operationUuids.push_back(node->instanceUuid);
}

const std::vector<std::string>& EditorModule::GetRawLayerToolLabels() const {
    if (!(m_Project->nodeEditor && m_Project->nodeEditor->layerId == Stack::Project::kProjectGraphId) &&
        m_Project->rawMode != Stack::RawWorkspace::RawProjectMode::UnifiedLayers)
    {
        static const std::vector<std::string> legacy{"Transform","Denoise","Color","Exposure","Curves","View"};
        return legacy;
    }
    // Source preparation and output presentation have their own surfaces.
    if (!SelectedEditingNode()) { static const std::vector<std::string> empty; return empty; }
    RefreshRawLayerToolPicker();
    return m_RawLayerToolPicker.labels;
}

int EditorModule::GetRawLayerToolIndex() const {
    if (!(m_Project->nodeEditor && m_Project->nodeEditor->layerId == Stack::Project::kProjectGraphId) &&
        m_Project->rawMode != Stack::RawWorkspace::RawProjectMode::UnifiedLayers) return GetRawLabToolIndex();
    const auto* selected = SelectedEditingNode();
    const auto uuid = selected ? selected->instanceUuid : std::string{};
    RefreshRawLayerToolPicker();
    const auto& choices = m_RawLayerToolPicker.operationUuids;
    for (std::size_t i=0;i<choices.size();++i) if (choices[i]==uuid) return static_cast<int>(i);
    return -1;
}

void EditorModule::RequestRawLayerToolIndex(int index) {
    RefreshRawLayerToolPicker();
    const auto& choices = m_RawLayerToolPicker.operationUuids;
    if (index>=0 && index<static_cast<int>(choices.size()) &&
        !m_RawLayerToolPicker.sourceUuid.empty() && choices[index]==m_RawLayerToolPicker.sourceUuid) {
        if (FinishWorkspaceInteraction()) OpenBracketingTool();
        return;
    }
    if (m_Project->nodeEditor && m_Project->nodeEditor->layerId == Stack::Project::kProjectGraphId &&
        !IsBracketingToolActive()) {
        RefreshRawLayerToolPicker();
        if (index < 0 || index >= static_cast<int>(choices.size())) return;
        const auto uuid = choices[index];
        for (const auto& node : m_Project->graph.GetNodes()) if (node.instanceUuid == uuid) {
            OpenRawProjectNodeEditor(node.id); return;
        }
        return;
    }
    if (m_Project->rawMode != Stack::RawWorkspace::RawProjectMode::UnifiedLayers) { RequestRawLabToolIndex(index); return; }
    RefreshRawLayerToolPicker();
    if (index<0 || index>=static_cast<int>(choices.size())) return;
    const auto uuid = choices[index];
    if (!FinishWorkspaceInteraction()) return;
    if(!m_RawLayerMaskWorkspace || !m_RawLayerMaskWorkspace->projectCanvas ||
        m_RawLayerMaskWorkspace->revision!=m_Project->rawLayers.Revision())
        OpenRawLayerMaskGraph(m_SelectedRawAdjustmentLayer,false);
    m_GraphEditorUsesRawLayer=true;
    if (!IsRawProjectCanvas()) return;
    for (const auto& node:GetNodeGraph().GetNodes()) if (node.instanceUuid==uuid && node.rawLayerOwnerId==m_RawLayerMaskWorkspace->layerId) {
        OpenRawProjectNodeEditor(node.id); return;
    }
}

bool EditorModule::RenderRawGraphOperationControls() {
    const auto* selected = SelectedEditingNode();
    if (selected && m_Project->nodeEditor && m_Project->nodeEditor->layerId == Stack::Project::kProjectGraphId &&
        !Stack::Editor::FindNodeEditor(*selected).primary) {
        const ImVec2 start=ImGui::GetCursorScreenPos();
        m_Sidebar.GetNodeGraphUI().RenderOperationControls(this,selected->id);
        m_RawActiveControlMinimum=start;
        m_RawActiveControlMaximum=ImVec2(ImGui::GetWindowPos().x+ImGui::GetWindowSize().x,
            std::max(start.y+80.f,ImGui::GetCursorScreenPos().y));
        m_RawActiveControlFrame=ImGui::GetFrameCount();
        return true;
    }
    if (!selected || Stack::Editor::FindNodeEditor(*selected).primary) return false;
    const auto uuid = selected->instanceUuid;
    const auto* owner = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    if (!owner) return false;
    const auto layerId = owner->id;
    // RAW image rendering uses the source graph, but this control edits the
    // selected project-canvas instance through the same commands as Graph.
    struct RestoreGraphTarget {
        bool& target;
        bool previous;
        ~RestoreGraphTarget() { target = previous; }
    } restore{m_GraphEditorUsesRawLayer, m_GraphEditorUsesRawLayer};
    m_GraphEditorUsesRawLayer = true;
    if (!m_RawLayerMaskWorkspace || !m_RawLayerMaskWorkspace->projectCanvas ||
        (m_RawLayerMaskWorkspace->revision != m_Project->rawLayers.Revision() && !m_RawLayerMaskWorkspace->dirty))
        OpenRawLayerMaskGraph(m_SelectedRawAdjustmentLayer,false);
    if (!m_RawLayerMaskWorkspace || !m_RawLayerMaskWorkspace->projectCanvas) return true;
    int id=0;
    for (const auto& node:GetNodeGraph().GetNodes())
        if (node.rawLayerOwnerId==layerId && node.instanceUuid==uuid) { id=node.id; break; }
    auto* node=GetNodeGraph().FindNode(id);
    if (!node) return true;
    const ImVec2 start=ImGui::GetCursorScreenPos();
    m_Sidebar.GetNodeGraphUI().RenderOperationControls(this,id);
    m_RawFloatingSurface.contentHeights[RawNodeEditorStateKey()] =
        std::max(80.f,ImGui::GetCursorScreenPos().y-start.y+16.f);
    m_RawActiveControlMinimum=start;
    m_RawActiveControlMaximum=ImVec2(ImGui::GetWindowPos().x+ImGui::GetWindowSize().x,
        std::max(start.y+80.f,ImGui::GetCursorScreenPos().y));
    m_RawActiveControlFrame=ImGui::GetFrameCount();
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
        m_RawLayerMaskWorkspace->dirty=false;
        if(m_Project->rawLayers.CancelGesture()) {MarkDirty();MarkRenderRefreshDirty();}
        OpenRawLayerMaskGraph(layerId,false);
        ImGui::ClearActiveID();
    } else FinishRawLayerGraphFrame();
    return true;
}
