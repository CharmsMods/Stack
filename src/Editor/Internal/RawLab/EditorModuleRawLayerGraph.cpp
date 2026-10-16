#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/LayerBase.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <algorithm>

using namespace Stack::Project;

bool EditorModule::IsEditingRawLayerMaskGraph() const {
    return m_GraphEditorUsesRawLayer && m_RawLayerMaskWorkspace.has_value();
}

void EditorModule::MarkGraphEdited(int touchedNodeId, bool affectsPixels) {
    if (IsEditingRawLayerMaskGraph()) {
        m_RawLayerMaskWorkspace->dirty = true;
        m_RawLayerMaskWorkspace->affectsPixels |= affectsPixels;
        return;
    }
    ObserveProjectGraphEdit(ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right));
    if (affectsPixels) MarkRenderDirty(touchedNodeId);
    else MarkDirty();
}

void EditorModule::OpenRawLayerMaskGraph(const std::string& layerId, bool activate) {
    if (!CommitRawLayerMaskGraph()) return;
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), layerId);
    if (!layer) return;
    if (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->projectCanvas &&
        m_RawLayerMaskWorkspace->revision == m_Project->rawLayers.Revision()) {
        // Both views use this same document. Reopening an unchanged canvas must
        // not recreate runtime effects or discard accepted thumbnail resources.
        auto& workspace = *m_RawLayerMaskWorkspace;
        if (IsBracketingActive()) {
            const auto* source=m_Project->graph.FindNode(ResolveRawWorkspaceStageOutputNodeId());
            auto* projected=workspace.graph.FindNode(workspace.projectCanvas->preparedSource);
            if (source && projected && source->kind==EditorNodeGraph::NodeKind::MultiFrameHdr) {
                projected->kind=source->kind;
                projected->instanceUuid=source->instanceUuid;
                projected->title=source->title;
                projected->definitionId=source->definitionId;
                projected->definitionVersion=source->definitionVersion;
                projected->definitionHash=source->definitionHash;
                projected->definitionResolved=source->definitionResolved;
                projected->definitionResolutionError=source->definitionResolutionError;
                projected->multiFrameHdr=source->multiFrameHdr;
            }
        }
        workspace.layerId = layer->id;
        workspace.animation = layer->animation;
        if (const auto* selected=SelectedEditingNode())
            for (const auto& node:workspace.graph.GetNodes())
                if ((node.rawLayerOwnerId==layer->id || node.id==workspace.projectCanvas->preparedSource) &&
                    node.instanceUuid==selected->instanceUuid) {
                    workspace.graph.SelectNode(node.id); break;
                }
        m_GraphEditorUsesRawLayer = true;
        m_SelectedRawAdjustmentLayer = layer->id == kRawBackgroundId ? std::string{} : layer->id;
        m_SelectedLayerIndex = -1;
        if (activate) { SwitchToSubWindow(EditorSubWindow::NodeGraph); RequestOpenEditorTab(); }
        return;
    }
    Stack::Editor::RawLayerMaskWorkspace workspace;
    workspace.layerId = layer->id;
    workspace.projectCanvas = BuildRawProjectGraph(m_Project->rawLayers.State());
    workspace.graph = std::move(workspace.projectCanvas->graph);
    if (IsBracketingActive()) {
        const auto* source=m_Project->graph.FindNode(ResolveRawWorkspaceStageOutputNodeId());
        auto* projected=workspace.graph.FindNode(workspace.projectCanvas->preparedSource);
        if (source && projected && source->kind==EditorNodeGraph::NodeKind::MultiFrameHdr) {
            // The first card represents the actual source-set editor, while
            // remaining the projection's immutable prepared-image endpoint.
            const auto position=projected->position;
            const int id=projected->id;
            *projected=*source;
            projected->id=id;
            projected->position=position;
            projected->role=Stack::GraphModel::NodeRole::OriginalImage;
            projected->rawLayerOwnerId.clear();
        }
    }
    workspace.animation = layer->animation;
    workspace.revision = m_Project->rawLayers.Revision();
    if (m_RawLayerMaskWorkspace) {
        const auto* previous=m_RawLayerMaskWorkspace->graph.FindNode(m_RawLayerMaskWorkspace->previewOutputNodeId);
        if (previous) for (const auto& node:workspace.graph.GetNodes())
            if (node.instanceUuid==previous->instanceUuid && node.rawLayerOwnerId==previous->rawLayerOwnerId) {
                workspace.previewOutputNodeId=node.id; break;
            }
    }
    const auto* selectedOperation=SelectedEditingNode();
    const auto selectedUuid=selectedOperation ? selectedOperation->instanceUuid : std::string{};
    for (const auto& node:workspace.graph.GetNodes())
        if ((node.rawLayerOwnerId==workspace.layerId || node.id==workspace.projectCanvas->preparedSource) &&
            node.instanceUuid==selectedUuid) {
            workspace.graph.SelectNode(node.id); break;
        }
    for (const auto& settings : Stack::GraphModel::LayerParameterRecords(workspace.graph)) {
        auto node = LayerRegistry::CreateLayerFromTypeId(settings.value("type", std::string()));
        if (!node) { m_RawLayerStatus = "A mask processing tool is unavailable."; return; }
        node->InitializeGL();
        Stack::GraphModel::PrepareLayerParameterDraft(*node,settings);
        workspace.layers.push_back(std::move(node));
    }
    m_Project->rawLayers.EndGesture();
    CancelCanvasTool();
    ClearTrackedToneCurveProbe();
    m_RawLayerMaskWorkspace = std::move(workspace);
    m_GraphEditorUsesRawLayer = true;
    m_SelectedRawAdjustmentLayer = layer->id == kRawBackgroundId ? std::string{} : layer->id;
    m_SelectedLayerIndex = -1;
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_PreviewDisplayedRevisions.clear();
    m_Sidebar.GetNodeGraphUI().ResetProjectInteractionState();
    if (activate) {
        SwitchToSubWindow(EditorSubWindow::NodeGraph);
        RequestOpenEditorTab();
    }
    MarkRenderRefreshDirty();
}

bool EditorModule::CommitRawLayerMaskGraph() {
    if (!m_RawLayerMaskWorkspace || !m_RawLayerMaskWorkspace->dirty) return true;
    auto candidate = m_Project->rawLayers.State();
    auto& workspace = *m_RawLayerMaskWorkspace;
    if (workspace.revision != m_Project->rawLayers.Revision()) {
        m_RawLayerStatus = "The layer changed while this graph edit was prepared. Reopen its graph before applying the edit.";
        return false;
    }
    auto* layer = FindRawAdjustmentLayer(candidate, workspace.layerId);
    if (!layer) { m_RawLayerStatus = "The edited layer no longer exists."; return false; }
    auto settings = nlohmann::json::array();
    for (const auto& node : workspace.layers) settings.push_back(node->Serialize());
    if (workspace.projectCanvas) {
        if (!ApplyRawProjectGraph(m_Project->rawLayers.State(),*workspace.projectCanvas,
                workspace.graph,settings,workspace.layerId,candidate,m_RawLayerStatus)) {
            workspace.dirty = false; workspace.affectsPixels = false;
            const auto message = m_RawLayerStatus;
            OpenRawLayerMaskGraph(workspace.layerId,false);
            m_RawLayerStatus = message;
            return false;
        }
    } else {
        layer->graph = workspace.graph;
        layer->animation = workspace.animation;
        RegisterRawMaskOutputs(candidate, workspace.layerId);
    }
    const bool gesture = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (gesture) m_Project->rawLayers.BeginGesture();
    if (!ApplyRawLayerStackEdit(std::move(candidate), workspace.affectsPixels)) {
        if (workspace.projectCanvas) {
            workspace.dirty = false; workspace.affectsPixels = false;
            const auto owner = workspace.layerId;
            const auto message = m_RawLayerStatus;
            OpenRawLayerMaskGraph(owner,false);
            m_RawLayerStatus = message;
            return false;
        }
        const auto* saved = FindRawAdjustmentLayer(m_Project->rawLayers.State(), workspace.layerId);
        if (saved) {
            workspace.graph = saved->graph; workspace.animation = saved->animation;
            const auto savedSettings=Stack::GraphModel::LayerParameterRecords(saved->graph);
            workspace.layers.resize(savedSettings.size());
            for (std::size_t i=0; i<workspace.layers.size(); ++i) {
                if (!workspace.layers[i]) {
                    workspace.layers[i] = LayerRegistry::CreateLayerFromTypeId(savedSettings[i].value("type",std::string{}));
                    if (workspace.layers[i]) workspace.layers[i]->InitializeGL();
                }
                if (workspace.layers[i]) Stack::GraphModel::PrepareLayerParameterDraft(*workspace.layers[i],savedSettings[i]);
            }
        }
        workspace.dirty = false; workspace.affectsPixels = false;
        return false;
    }
    workspace.revision = m_Project->rawLayers.Revision();
    for (auto& node : workspace.graph.GetNodes())
        if (workspace.projectCanvas && node.id != workspace.projectCanvas->preparedSource &&
            node.id != workspace.projectCanvas->output && node.rawLayerOwnerId.empty()) node.rawLayerOwnerId = workspace.layerId;
    workspace.dirty = false;
    workspace.affectsPixels = false;
    return true;
}

void EditorModule::FinishRawLayerGraphFrame() {
    if (!IsEditingRawLayerMaskGraph()) return;
    if (!CommitRawLayerMaskGraph()) {
        // The rejected candidate has been restored from the document.
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsAnyItemActive())
        m_Project->rawLayers.EndGesture();
}

void EditorModule::RenderRawLayerGraphToolbar() {
    if (!IsEditingRawLayerMaskGraph()) return;
    const auto id=m_RawLayerMaskWorkspace->layerId;
    if (!FindRawAdjustmentLayer(m_Project->rawLayers.State(),id)) {
        m_RawLayerMaskWorkspace.reset();return;
    }
    if (m_RawLayerMaskWorkspace->revision!=m_Project->rawLayers.Revision() && !m_RawLayerMaskWorkspace->dirty)
        OpenRawLayerMaskGraph(id,false);
    // Selection comes from the canvas, and Assets lives in the shared sidebar.
    // Keep synchronization here without reserving a redundant header strip.
    if (!m_RawLayerStatus.empty()) ImGui::TextWrapped("%s",m_RawLayerStatus.c_str());
}
