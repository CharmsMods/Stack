#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Project/RawLayerStack.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/LayerBase.h"

Stack::Editor::GraphEditorContext EditorModule::GetGraphEditorContext() {
    BeginProjectGraphHistory();
    if (IsEditingRawLayerMaskGraph() && m_RawLayerMaskWorkspace->projectCanvas)
        return GetRawProjectGraphEditorContext();
    Stack::Editor::GraphEditorContext context;
    context.documentId = GetProjectDocumentId();
    context.graphId = IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->layerId : "project";
    context.graph = &GetNodeGraph();
    context.revision = context.graph->GetStructureRevision();
    context.documentRevision = m_Project->editRevision;
    const auto document = context.documentId;
    const auto graphId = context.graphId;
    const auto layerRevision = m_Project->rawLayers.Revision();
    const auto projectRevision = m_Project->editRevision;
    const auto stillOwned = [this, document, graphId] {
        return GetProjectDocumentId() == document &&
            (IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->layerId : "project") == graphId;
    };
    context.queryAvailableNodes = [this, stillOwned](auto endpoint) {
        if (!stillOwned()) return std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry>{};
        const auto accepts = [this](const EditorNodeGraph::Graph& graph) {
            std::string error;
            if (IsEditingRawLayerMaskGraph()) {
                auto candidate = m_Project->rawLayers.State();
                auto* layer = Stack::Project::FindRawAdjustmentLayer(candidate,m_RawLayerMaskWorkspace->layerId);
                if (!layer) return false;
                layer->graph = graph;
                return Stack::Project::ValidateRawLayerStack(candidate,error,&m_Project->graph,ResolveRawWorkspaceStageOutputNodeId());
            }
            return !(IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) ||
                Stack::Project::ValidateRawLayerStack(m_Project->rawLayers.State(),error,&graph,ResolveRawWorkspaceStageOutputNodeId());
        };
        return Stack::GraphModel::QueryAvailableNodes(GetNodeGraph(),endpoint,
            IsEditingRawLayerMaskGraph() ? EditorNodeGraphDefinitions::LiveGraphRole::RawLayer : EditorNodeGraphDefinitions::LiveGraphRole::Composition,accepts);
    };
    context.canConnect = [this, stillOwned](int from, const std::string& output, int to, const std::string& input, std::string* reason) {
        if (!stillOwned()) return false;
        const auto analysisGraph = GetNodeGraph().CloneForAnalysis();
        auto proposal = Stack::GraphModel::ProposeEdit(analysisGraph,GetNodeGraph().GetStructureRevision(),[&](auto& graph) {
            std::string error;
            if (!graph.TryConnectSockets(from,output,to,input,&error)) throw std::runtime_error(error);
        });
        if (!proposal.analysis.valid) { if (reason) *reason = proposal.analysis.errors.front(); return false; }
        std::string error;
        bool valid = true;
        if (IsEditingRawLayerMaskGraph()) {
            auto candidate = m_Project->rawLayers.State();
            auto* layer = Stack::Project::FindRawAdjustmentLayer(candidate,m_RawLayerMaskWorkspace->layerId);
            if (!layer) return false;
            layer->graph = std::move(proposal.candidate);
            valid = Stack::Project::ValidateRawLayerStack(candidate,error,&m_Project->graph,ResolveRawWorkspaceStageOutputNodeId());
        } else if (IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive())
            valid = Stack::Project::ValidateRawLayerStack(m_Project->rawLayers.State(),error,&proposal.candidate,ResolveRawWorkspaceStageOutputNodeId());
        if (reason) *reason = std::move(error);
        return valid;
    };
    context.animation = &GetGraphAnimation();
    context.applyDocumentEdit = [this, stillOwned, layerRevision, projectRevision, document](Stack::GraphModel::EditProposal proposal,
        nlohmann::json settings, Stack::Timeline::TimelineAnimationState animation, std::string& error) {
        if (!stillOwned()) { error = "The edit belongs to another graph or project."; return false; }
        if (projectRevision != m_Project->editRevision) { error = "The project changed while this edit was prepared."; return false; }
        if (!IsEditingRawLayerMaskGraph()) return ApplyProjectGraphDocumentEdit(document,projectRevision,
            std::move(proposal),std::move(animation),ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right),error);
        EditorNodeGraph::Graph graph;
        if (!Stack::GraphModel::ApplyEdit(graph, GetNodeGraph().GetStructureRevision(), std::move(proposal), error)) return false;
        Stack::GraphModel::ReindexLayerParameterRecords(graph);
        settings=Stack::GraphModel::LayerParameterRecords(graph);
        if (IsEditingRawLayerMaskGraph()) {
            if (layerRevision != m_Project->rawLayers.Revision()) {
                error = "The layer document changed while this edit was prepared.";
                return false;
            }
            auto candidate = m_Project->rawLayers.State();
            auto* layer = Stack::Project::FindRawAdjustmentLayer(candidate, m_RawLayerMaskWorkspace->layerId);
            if (!layer) { error = "The edited layer no longer exists."; return false; }
            layer->graph = graph; layer->animation = animation;
            Stack::Project::RegisterRawMaskOutputs(candidate, layer->id);
            if (!Stack::Project::ValidateRawLayerStack(candidate, error, &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId())) return false;
        }
        else if ((IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) &&
            !Stack::Project::ValidateRawLayerStack(m_Project->rawLayers.State(), error, &graph, ResolveRawWorkspaceStageOutputNodeId())) return false;
        // Runtime adapters are created only after the authored candidate passes
        // the model checks. Failed construction changes no document records.
        std::vector<std::shared_ptr<LayerBase>> layers;
        try {
            for (std::size_t i = 0; i < settings.size(); ++i) {
                if (i < GetLayers().size() && GetLayers()[i] && Stack::GraphModel::CaptureLayerParameters(*GetLayers()[i]) == settings[i]) {
                    layers.push_back(GetLayers()[i]); continue;
                }
                auto layer = LayerRegistry::CreateLayerFromTypeId(settings[i].value("type", std::string{}));
                if (!layer) { error = "An operation in this edit is not installed."; return false; }
                layer->InitializeGL(); Stack::GraphModel::PrepareLayerParameterDraft(*layer,settings[i]); layers.push_back(std::move(layer));
            }
        } catch (const std::exception& exception) { error = exception.what(); return false; }
        if (IsEditingRawLayerMaskGraph()) {
            auto candidate = m_Project->rawLayers.State();
            auto* layer = Stack::Project::FindRawAdjustmentLayer(candidate, m_RawLayerMaskWorkspace->layerId);
            layer->graph = graph; layer->animation = animation;
            Stack::Project::RegisterRawMaskOutputs(candidate, layer->id);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_Project->rawLayers.BeginGesture();
            if (!m_Project->rawLayers.Apply(std::move(candidate), error)) return false;
            m_RawLayerMaskWorkspace->revision = m_Project->rawLayers.Revision();
            m_RawLayerMaskWorkspace->dirty = false;
            m_RawLayerMaskWorkspace->affectsPixels = false;
        }
        Stack::GraphModel::RetainImageStorage(graph, GetNodeGraph());
        GetNodeGraph() = std::move(graph); GetLayers() = std::move(layers);
        GetGraphAnimation() = std::move(animation);
        if (IsEditingRawLayerMaskGraph()) { MarkDirty(); MarkRenderRefreshDirty(); }
        else MarkGraphEdited();
        return true;
    };
    const auto applyDocumentEdit = context.applyDocumentEdit;
    context.applyEdit = [this, applyDocumentEdit](Stack::GraphModel::EditProposal proposal, std::string& error) {
        // applyDocumentEdit derives operation records from the authored candidate.
        return applyDocumentEdit(std::move(proposal), {}, GetGraphAnimation(), error);
    };
    context.selectNode = [this, stillOwned](int id) { if (stillOwned()) SelectGraphNode(id); };
    context.inspectParameters = [this, stillOwned](int id) {
        if (!stillOwned()) return;
        OpenRawProjectNodeEditor(id);
    };
    context.requestPreview = [this, stillOwned](int id, const std::string&) {
        if (!stillOwned() || IsViewMode()) return;
        if (IsEditingRawLayerMaskGraph()) m_RawLayerMaskWorkspace->previewOutputNodeId = id;
        else m_CompositeSelectedOutputNodeId = id;
        MarkRenderRefreshDirty();
    };
    return context;
}
