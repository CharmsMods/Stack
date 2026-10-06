#include "Editor/EditorModule.h"
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
    if (affectsPixels) MarkRenderDirty(touchedNodeId);
    else MarkDirty();
}

void EditorModule::OpenRawLayerMaskGraph(const std::string& layerId) {
    if (!CommitRawLayerMaskGraph()) return;
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), layerId);
    if (!layer) return;
    Stack::Editor::RawLayerMaskWorkspace workspace;
    workspace.layerId = layer->id;
    workspace.graph = layer->graph;
    workspace.animation = layer->animation;
    workspace.revision = m_Project->rawLayers.Revision();
    if (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->layerId == layerId)
        workspace.previewOutputNodeId = m_RawLayerMaskWorkspace->previewOutputNodeId;
    for (const auto& settings : layer->processingSettings) {
        auto node = LayerRegistry::CreateLayerFromTypeId(settings.value("type", std::string()));
        if (!node) { m_RawLayerStatus = "A mask processing tool is unavailable."; return; }
        node->InitializeGL();
        node->Deserialize(settings);
        workspace.layers.push_back(std::move(node));
    }
    m_Project->rawLayers.EndGesture();
    CancelCanvasTool();
    ClearTrackedToneCurveProbe();
    m_FrequencyGraphUndo.reset();
    m_FrequencyGraphRedo.reset();
    m_CustomMaskUndoStacks.clear();
    m_CustomMaskRedoStacks.clear();
    m_CustomMaskPaintingNodes.clear();
    m_RawLayerMaskWorkspace = std::move(workspace);
    m_GraphEditorUsesRawLayer = true;
    m_SelectedRawAdjustmentLayer = layer->id == kRawBackgroundId ? std::string{} : layer->id;
    m_SelectedLayerIndex = -1;
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_PreviewDisplayedRevisions.clear();
    m_Sidebar.GetNodeGraphUI().ResetProjectInteractionState();
    SwitchToSubWindow(EditorSubWindow::NodeGraph);
    RequestOpenEditorTab();
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
    layer->graph = workspace.graph;
    layer->animation = workspace.animation;
    layer->processingSettings = nlohmann::json::array();
    for (const auto& node : workspace.layers) layer->processingSettings.push_back(node->Serialize());
    RegisterRawMaskOutputs(candidate, workspace.layerId);
    const bool gesture = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (gesture) m_Project->rawLayers.BeginGesture();
    if (!ValidateRawLayerStack(candidate, m_RawLayerStatus, &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId()) ||
        !m_Project->rawLayers.Apply(std::move(candidate), m_RawLayerStatus, workspace.affectsPixels)) {
        const auto* saved = FindRawAdjustmentLayer(m_Project->rawLayers.State(), workspace.layerId);
        if (saved) {
            workspace.graph = saved->graph; workspace.animation = saved->animation;
            workspace.layers.resize(saved->processingSettings.size());
            for (std::size_t i=0; i<workspace.layers.size(); ++i) {
                if (!workspace.layers[i]) {
                    workspace.layers[i] = LayerRegistry::CreateLayerFromTypeId(saved->processingSettings[i].value("type",std::string{}));
                    if (workspace.layers[i]) workspace.layers[i]->InitializeGL();
                }
                if (workspace.layers[i]) workspace.layers[i]->Deserialize(saved->processingSettings[i]);
            }
        }
        workspace.dirty = false; workspace.affectsPixels = false;
        return false;
    }
    workspace.revision = m_Project->rawLayers.Revision();
    workspace.dirty = false;
    m_Project->NoteEdit(ImGui::GetTime());
    if (workspace.affectsPixels) MarkRenderRefreshDirty();
    workspace.affectsPixels = false;
    return true;
}

void EditorModule::FinishRawLayerGraphFrame() {
    if (!IsEditingRawLayerMaskGraph()) return;
    if (!CommitRawLayerMaskGraph()) {
        // The rejected candidate has been restored from the document.
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_Project->rawLayers.EndGesture();
}

void EditorModule::RenderRawLayerGraphToolbar() {
    if (!IsEditingRawLayerMaskGraph()) return;
    const auto id = m_RawLayerMaskWorkspace->layerId;
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), id);
    if (!layer) { m_RawLayerMaskWorkspace.reset(); return; }
    if (m_RawLayerMaskWorkspace->revision != m_Project->rawLayers.Revision() && !m_RawLayerMaskWorkspace->dirty) {
        OpenRawLayerMaskGraph(id);
        layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), id);
    }
    ImGui::Text("%s / Graph", layer->name.c_str());
    const auto connectedOutputs = GetNodeGraph().GetConnectedOutputNodeIds();
    const auto* previewNode = GetNodeGraph().FindNode(m_RawLayerMaskWorkspace->previewOutputNodeId);
    if (m_RawLayerMaskWorkspace->previewOutputNodeId > 0 && !previewNode)
        m_RawLayerStatus = "The requested output was deleted.";
    if (ImGui::BeginCombo("Preview", previewNode ? previewNode->title.c_str() : "Layer result")) {
        if (ImGui::Selectable("Layer result", !previewNode)) {
            m_RawLayerMaskWorkspace->previewOutputNodeId = 0;
            MarkRenderRefreshDirty();
        }
        for (const auto& output : layer->graph.GetNodes()) {
            if (output.kind != EditorNodeGraph::NodeKind::Output) continue;
            ImGui::PushID(output.instanceUuid.c_str());
            if (ImGui::Selectable(output.title.c_str(), output.id == m_RawLayerMaskWorkspace->previewOutputNodeId)) {
                m_RawLayerMaskWorkspace->previewOutputNodeId = output.id;
                MarkRenderRefreshDirty();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("RAW controls") && CommitRawLayerMaskGraph()) RequestOpenRawLabTab();
    ImGui::SameLine();
    if (ImGui::SmallButton("View / Output") && CommitRawLayerMaskGraph()) {
        m_RawWorkspaceLabUi.activeTool = RawLabTool::View;
        RequestOpenRawLabTab();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Project Graph") && CommitRawLayerMaskGraph()) {
        m_Project->rawLayers.EndGesture();
        CancelCanvasTool();
        ClearTrackedToneCurveProbe();
        m_FrequencyGraphUndo.reset();
        m_FrequencyGraphRedo.reset();
        m_CustomMaskUndoStacks.clear();
        m_CustomMaskRedoStacks.clear();
        m_CustomMaskPaintingNodes.clear();
        m_RawLayerMaskWorkspace.reset();
        m_SelectedLayerIndex = -1;
        m_PreviewPixelCache.clear();
        m_PreviewRequestedGenerations.clear();
        m_PreviewCompletedGenerations.clear();
        m_PreviewDisplayedRevisions.clear();
        m_Sidebar.GetNodeGraphUI().ResetProjectInteractionState();
        MarkRenderRefreshDirty();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo##MaskGraph")) UndoRawLayerEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Redo##MaskGraph")) RedoRawLayerEdit();
    const int selectedId = GetNodeGraph().GetSelectedNodeId();
    const auto* selected = GetNodeGraph().FindNode(selectedId);
    if (selected && selected->role == Stack::GraphModel::NodeRole::Ordinary &&
        (selected->kind == EditorNodeGraph::NodeKind::RawOperation || selected->kind == EditorNodeGraph::NodeKind::Layer)) {
        if (ImGui::SmallButton("Process earlier")) {
            auto context = GetGraphEditorContext();
            auto proposal = Stack::GraphModel::ProposeSerialMove(*context.graph,context.revision,selectedId,false);
            context.applyEdit(std::move(proposal),m_RawLayerStatus);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Process later")) {
            auto context = GetGraphEditorContext();
            auto proposal = Stack::GraphModel::ProposeSerialMove(*context.graph,context.revision,selectedId,true);
            context.applyEdit(std::move(proposal),m_RawLayerStatus);
        }
    }
    selected = GetNodeGraph().FindNode(selectedId);
    if (selected && ImGui::BeginCombo("##PublishResult", "Use this result in another layer")) {
        const auto producerUuid = selected->instanceUuid;
        const auto sockets = GetNodeGraph().GetSockets(*selected,false);
        std::vector<std::pair<std::string,std::string>> results;
        if (selected->kind == EditorNodeGraph::NodeKind::Output) results.push_back({"imageOut",selected->title});
        else for (const auto& socket : sockets) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Output) continue;
            const auto type = socket.logicalType;
            if (type == Stack::NodeMath::LogicalValueType::ColorImage || EditorNodeGraph::IsSingleChannelValue(type) ||
                type == Stack::NodeMath::LogicalValueType::Scalar || type == Stack::NodeMath::LogicalValueType::ComplexSpectrum ||
                type == Stack::NodeMath::LogicalValueType::FrequencyResponse || type == Stack::NodeMath::LogicalValueType::SpectrumMagnitude ||
                type == Stack::NodeMath::LogicalValueType::SpectrumPhase)
                results.push_back({socket.id,socket.label});
        }
        struct Destination { std::string id,name; };
        std::vector<Destination> destinations;
        for (const auto* destination : RawLayersInOrder(m_Project->rawLayers.State()))
            if (destination->id != id) destinations.push_back({destination->id,destination->name});
        for (const auto& destination : destinations) for (const auto& output : results) {
            const auto label = destination.name + " / " + output.second;
            if (ImGui::Selectable(label.c_str()) && CommitRawLayerMaskGraph()) {
                auto candidate = m_Project->rawLayers.State();
                if (PublishRawLayerResult(candidate,{id,producerUuid,output.first},destination.id,m_RawLayerStatus) &&
                    ApplyRawLayerStackEdit(std::move(candidate))) OpenRawLayerMaskGraph(id);
            }
        }
        if (destinations.empty()) ImGui::TextUnformatted("Add another RAW layer to receive this result.");
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("##PipelineSource", "Add published reference")) {
        struct Choice { Stack::GraphModel::Endpoint endpoint; std::string name; bool mask; Stack::NodeMath::LogicalValueType type; };
        std::vector<Choice> choices;
        for (const auto* entry : RawLayersInOrder(m_Project->rawLayers.State())) {
            for (const auto& node : entry->graph.GetNodes()) {
                if (node.kind == EditorNodeGraph::NodeKind::Output)
                    choices.push_back({RawLayerEndpoint(*entry, node), entry->name + " / " + node.title, node.outputSettings.maskOutput, node.outputSettings.publishedType});
            }
        }
        for (const auto& node : m_Project->graph.GetNodes()) if (node.kind == EditorNodeGraph::NodeKind::Output)
            choices.push_back({{"project",node.instanceUuid,"imageOut"},"Project / " + node.title,node.outputSettings.maskOutput,node.outputSettings.publishedType});
        for (const auto& choice : choices) {
            ImGui::PushID((choice.endpoint.graphId + choice.endpoint.nodeUuid).c_str());
            if (ImGui::Selectable(choice.name.c_str()) && CommitRawLayerMaskGraph()) {
                auto candidate = m_Project->rawLayers.State();
                auto* owner = FindRawAdjustmentLayer(candidate, id);
                auto* reference = choice.mask ? owner->graph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid, {0, -250}) :
                    owner->graph.AddImageNode({}, {0, -250});
                reference->role = Stack::GraphModel::NodeRole::Reference;
                reference->reference = choice.endpoint;
                reference->referenceType = choice.type;
                reference->title = choice.name;
                if (ApplyRawLayerStackEdit(std::move(candidate))) OpenRawLayerMaskGraph(id);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (!m_RawLayerStatus.empty()) ImGui::TextWrapped("%s", m_RawLayerStatus.c_str());
    ImGui::Separator();
}
