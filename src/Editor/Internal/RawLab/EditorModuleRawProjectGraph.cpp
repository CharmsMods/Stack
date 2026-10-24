#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Editor/RawLayerTools.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/LayerBase.h"
#include "Project/RawProjectGraph.h"
#include "Project/GraphSnapshotLookup.h"

#include <algorithm>

using namespace Stack::Project;

Stack::Editor::GraphEditorContext EditorModule::GetRawProjectGraphEditorContext() {
    Stack::Editor::GraphEditorContext context;
    context.documentId = GetProjectDocumentId();
    context.graphId = "$raw-project";
    context.graph = &GetNodeGraph();
    context.revision = context.graph->GetStructureRevision();
    context.documentRevision = m_Project->editRevision;
    context.animation = &m_RawLayerMaskWorkspace->animation;
    const auto document = context.documentId;
    const auto revision = m_Project->rawLayers.Revision();
    const auto projectRevision = m_Project->editRevision;
    const auto owned = [this,document,revision,projectRevision] {
        return GetProjectDocumentId() == document && IsEditingRawLayerMaskGraph() &&
            m_RawLayerMaskWorkspace->projectCanvas && m_Project->rawLayers.Revision() == revision &&
            m_Project->editRevision == projectRevision;
    };
    const auto settingsFor = [this] {
        auto settings = nlohmann::json::array();
        for (const auto& item : GetLayers()) settings.push_back(item->Serialize());
        return settings;
    };
    const auto accepts = [this,owned,settingsFor](const EditorNodeGraph::Graph& graph, std::string& error) {
        if (!owned()) { error = "The project changed while this edit was prepared."; return false; }
        auto settings = settingsFor();
        for (const auto& node : graph.GetNodes()) if (node.kind == EditorNodeGraph::NodeKind::Layer && node.layerIndex >= 0)
            while (settings.size() <= static_cast<std::size_t>(node.layerIndex)) settings.push_back({{"type",node.typeId}});
        return m_Project->rawLayers.PrepareGraphEdit(*m_RawLayerMaskWorkspace->projectCanvas,
            graph,settings,m_RawLayerMaskWorkspace->layerId,error,&m_Project->graph,
            ResolveRawWorkspaceStageOutputNodeId(),m_Project->editRevision).has_value();
    };
    context.queryAvailableNodes = [this,owned,accepts](auto endpoint) {
        if (!owned()) return std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry>{};
        auto entries = Stack::GraphModel::QueryAvailableNodes(GetNodeGraph(),endpoint,
            EditorNodeGraphDefinitions::LiveGraphRole::RawLayer,[&](const auto& graph) {
                std::string error;
                return accepts(graph,error);
            });
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto& entry) {
            using Kind = EditorNodeGraph::MaskGeneratorKind;
            return entry.kind == EditorNodeGraph::NodeKind::MaskGenerator &&
                (entry.value == static_cast<int>(Kind::LinearGradient) ||
                 entry.value == static_cast<int>(Kind::RadialGradient) || entry.value == static_cast<int>(Kind::Square));
        }),entries.end());
        return entries;
    };
    context.canConnect = [this,owned,accepts](int from,const std::string& output,int to,const std::string& input,std::string* error) {
        if (!owned()) return false;
        auto graph = GetNodeGraph().CloneForAnalysis();
        std::string reason;
        const bool valid = graph.TryConnectSockets(from,output,to,input,&reason) && accepts(graph,reason);
        if (error) *error = std::move(reason);
        return valid;
    };
    const auto commitGraph = [this,owned](EditorNodeGraph::Graph graph,nlohmann::json settings,
        Stack::Timeline::TimelineAnimationState animation,std::string& error) {
        if (!owned()) { error = "The project changed while this edit was prepared."; return false; }
        Stack::GraphModel::ReindexLayerParameterRecords(graph);
        settings=Stack::GraphModel::LayerParameterRecords(graph);
        RawGraphGroupOwners groupOwners;
        auto prepared = m_Project->rawLayers.PrepareGraphEdit(*m_RawLayerMaskWorkspace->projectCanvas,
            graph,settings,m_RawLayerMaskWorkspace->layerId,error,&m_Project->graph,
            ResolveRawWorkspaceStageOutputNodeId(),m_Project->editRevision,&groupOwners);
        if (!prepared) return false;
        std::vector<std::shared_ptr<LayerBase>> runtime;
        try {
            for (std::size_t i=0;i<settings.size();++i) {
                if (i<GetLayers().size() && GetLayers()[i] && Stack::GraphModel::CaptureLayerParameters(*GetLayers()[i])==settings[i]) {
                    runtime.push_back(GetLayers()[i]); continue;
                }
                auto item = LayerRegistry::CreateLayerFromTypeId(settings[i].value("type",std::string{}));
                if (!item) { error = "An operation in this graph is unavailable."; return false; }
                item->InitializeGL(); Stack::GraphModel::PrepareLayerParameterDraft(*item,settings[i]); runtime.push_back(std::move(item));
            }
        } catch (const std::exception& failure) { error = failure.what(); return false; }
        if (!owned()) { error = "The project changed while this edit was prepared."; return false; }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) m_Project->rawLayers.BeginGesture();
        if (!m_Project->rawLayers.Commit(std::move(*prepared),error,true,false,m_Project->editRevision,&m_Project->graph)) return false;
        auto& workspace = *m_RawLayerMaskWorkspace;
        workspace.projectCanvas->groupOwners=std::move(groupOwners);
        for (auto& node : graph.GetNodes()) if (node.id != workspace.projectCanvas->preparedSource &&
            node.id != workspace.projectCanvas->output) {
            if (node.rawLayerOwnerId.empty()) node.rawLayerOwnerId = workspace.layerId;
            const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(),node.rawLayerOwnerId);
            if (layer) for (const auto& authored : layer->graph.GetNodes()) if (authored.instanceUuid == node.instanceUuid) {
                node.kind = authored.kind;
                node.rawCoverage = authored.rawCoverage;
                node.definitionId = authored.definitionId;
                node.definitionVersion = authored.definitionVersion;
                node.definitionHash = authored.definitionHash;
                node.definitionResolved = authored.definitionResolved;
                node.definitionResolutionError = authored.definitionResolutionError;
                break;
            }
        }
        Stack::GraphModel::RetainImageStorage(graph,workspace.graph);
        workspace.graph = std::move(graph);
        workspace.layers = std::move(runtime);
        workspace.animation = std::move(animation);
        workspace.revision = m_Project->rawLayers.Revision();
        workspace.dirty = false; workspace.affectsPixels = false;
        ReconcileRawNodeEditorSelection();
        MarkDirty(); MarkRenderRefreshDirty();
        NoteRawWorkspaceRecipePreviewEdit(m_Project->rawLayers.GestureActive());
        return true;
    };
    context.applyDocumentEdit = [this,owned,commitGraph](Stack::GraphModel::EditProposal proposal,nlohmann::json settings,
        Stack::Timeline::TimelineAnimationState animation,std::string& error) {
        if (!owned() || proposal.sourceRevision != GetNodeGraph().GetStructureRevision()) {
            error = "The project changed while this edit was prepared."; return false;
        }
        if (!proposal.analysis.valid) {
            error = proposal.analysis.errors.empty() ? "The proposed edit is invalid." : proposal.analysis.errors.front();
            return false;
        }
        // Mutable proposals are not trusted validation tokens. The final owned
        // RAW document is validated by commitGraph, including composition edges.
        return commitGraph(std::move(proposal.candidate),std::move(settings),std::move(animation),error);
    };
    context.applyCommand = [this,owned,commitGraph](const auto& edit,std::string& error) {
        if (!owned()) { error = "The project changed while this edit was prepared."; return false; }
        auto candidate = GetNodeGraph().CloneForAnalysis();
        try { edit(candidate); }
        catch (const std::exception& failure) { error=failure.what(); return false; }
        return commitGraph(std::move(candidate),{},m_RawLayerMaskWorkspace->animation,error);
    };
    const auto apply = context.applyDocumentEdit;
    context.applyEdit = [this,apply](Stack::GraphModel::EditProposal proposal,std::string& error) {
        return apply(std::move(proposal),{},m_RawLayerMaskWorkspace->animation,error);
    };
    context.selectNode = [this,owned](int id) { if (owned()) SelectGraphNode(id); };
    context.inspectParameters = [this,owned](int id) {
        if (owned() && !OpenRawProjectNodeEditor(id)) SwitchToComplexNodeSubWindow(id);
    };
    context.requestPreview = [this,owned](int id,const std::string&) {
        if (!owned()) return;
        if (IsViewMode()) return;
        m_RawLayerMaskWorkspace->previewOutputNodeId = id;
        MarkRenderRefreshDirty();
    };
    return context;
}

bool EditorModule::OpenRawProjectNodeEditor(int nodeId) {
    if (!IsEditingRawLayerMaskGraph()) {
        const auto* node=m_Project->graph.FindNode(nodeId);
        if (node && node->kind==EditorNodeGraph::NodeKind::MultiFrameHdr &&
            node->multiFrameHdr.bracketingSource && m_Project->snapshot && IsBracketingActive() &&
            node->multiFrameHdr.sourceSetId==m_Project->snapshot->activeSourceSetId) {
            if (!FinishWorkspaceInteraction()) return true;
            OpenBracketingTool();
            SelectGraphNode(nodeId);
            RequestOpenRawLabTab();
            return true;
        }
        const auto definition=node ? EditorNodeGraphDefinitions::ResolveNodeDefinition(m_Project->graph,*node) :
            EditorNodeGraphDefinitions::ResolvedNodeDefinition{};
        if (!node || !definition || definition.ParameterCount()==0) return false;
        if (!FinishWorkspaceInteraction()) return true;
        const NodeEditTarget target{GetProjectDocumentId(),kProjectGraphId,node->instanceUuid};
        if (!m_Project->nodeEditor || *m_Project->nodeEditor != target)
            m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();
        m_Project->nodeEditor=target;
        const auto& editor = Stack::Editor::FindNodeEditor(*node);
        m_RawWorkspaceLabUi.activeTool=editor.legacyTool.value_or(RawLabTool::GraphNode);
        if (editor.toneView >= 0) m_RawWorkspaceLabUi.sceneToneView=editor.toneView;
        SelectGraphNode(nodeId);
        SetWorkspaceMode(WorkspaceMode::View);
        return true;
    }
    if (!IsEditingRawLayerMaskGraph() || !m_RawLayerMaskWorkspace->projectCanvas) return false;
    const auto* node = GetNodeGraph().FindNode(nodeId);
    if (!node) return false;
    const auto kind = node->kind;
    const auto role = node->role;
    const auto owner = node->rawLayerOwnerId;
    const auto uuid = node->instanceUuid;
    if (!CommitRawLayerMaskGraph()) return true;
    const auto* layerOwner=FindRawAdjustmentLayer(m_Project->rawLayers.State(),owner);
    const auto choices=layerOwner ? Stack::Editor::RawLayerToolNodes(layerOwner->graph) : std::vector<const EditorNodeGraph::Node*>{};
    const bool hasNoEditableSurface=layerOwner && role==Stack::GraphModel::NodeRole::Ordinary &&
        kind!=EditorNodeGraph::NodeKind::Output && std::none_of(choices.begin(),choices.end(),
            [&](const auto* tool){return tool->instanceUuid==uuid;});
    if (hasNoEditableSurface) {
        // Connection-only or unresolved instances stay selected in Graph.
        // Editable coverage and image operations share the same View path.
        SelectGraphNode(nodeId);
        const EditorNodeGraph::Node* authored=nullptr;
        if (layerOwner) for (const auto& candidate:layerOwner->graph.GetNodes())
            if (candidate.instanceUuid==uuid) { authored=&candidate; break; }
        const auto definition=authored ? EditorNodeGraphDefinitions::ResolveNodeDefinition(layerOwner->graph,*authored) :
            EditorNodeGraphDefinitions::ResolvedNodeDefinition{};
        if (!definition || definition.ParameterCount()==0) return true;
    }
    m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();
    m_Project->nodeEditor.reset();
    if (nodeId == m_RawLayerMaskWorkspace->projectCanvas->preparedSource) {
        SelectRawAdjustmentLayer({});
        m_Project->nodeEditor.reset();
        if (IsBracketingActive()) {
            OpenBracketingTool();
            SelectGraphNode(nodeId);
            RequestOpenRawLabTab();
            return true;
        }
        m_RawWorkspaceLabUi.activeTool = RawLabTool::Light;
        RequestOpenRawLabTab(); return true;
    }
    if (nodeId == m_RawLayerMaskWorkspace->projectCanvas->output) {
        m_RawWorkspaceLabUi.activeTool = RawLabTool::View;
        RequestOpenRawLabTab(); return true;
    }
    if (kind == EditorNodeGraph::NodeKind::RawOperation) {
        SelectRawAdjustmentLayer(owner);
        SelectGraphNode(nodeId);
        RequestOpenRawLabTab(); return true;
    }
    if (role == Stack::GraphModel::NodeRole::CurrentImage || role == Stack::GraphModel::NodeRole::LayerResult) {
        SelectRawAdjustmentLayer(owner);
        // Layer selection already restores that layer's selected image tool.
        // Keep Graph on the same instance instead of replacing it with the
        // first ordinary node, which may be a mask or an unrelated helper.
        if (const auto* selected = SelectedEditingNode()) {
            const auto selectedUuid = selected->instanceUuid;
            for (const auto& item : GetNodeGraph().GetNodes())
                if (item.rawLayerOwnerId == owner && item.instanceUuid == selectedUuid) {
                    SelectGraphNode(item.id); break;
                }
        }
        RequestOpenRawLabTab(); return true;
    }
    if (!owner.empty()) {
        SelectRawAdjustmentLayer(owner);
        SelectGraphNode(nodeId);
        SelectRawNodeEditor(owner,uuid);
        if (kind == EditorNodeGraph::NodeKind::MaskGenerator || kind == EditorNodeGraph::NodeKind::MaskTexture) {
            const auto* layer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),owner);
            for (const auto& output:layer->maskOutputs) {
                const auto* published=FindRawPublishedNode(*layer,output);
                const auto* input=published ? layer->graph.FindInputLink(published->id,"imageIn") : nullptr;
                const auto* generator=published && (published->kind==EditorNodeGraph::NodeKind::MaskGenerator || published->kind==EditorNodeGraph::NodeKind::MaskTexture) ? published :
                    input ? layer->graph.FindNode(input->fromNodeId) : nullptr;
                if (generator && generator->instanceUuid==uuid) {
                    m_EditingRawLayerMask=RawMaskReference{owner,output.id};
                    m_RawLayerMaskGenerator=generator->id;
                    break;
                }
            }
        }
        RequestOpenRawLabTab();
        return true;
    }
    return false;
}

void EditorModule::BindBracketingSourceNodeEditor() {
    if (!m_Project->snapshot || !IsBracketingActive()) return;
    const auto* set=FindSourceSet(*m_Project->snapshot,m_Project->snapshot->activeSourceSetId);
    if (!set) return;
    for (const auto& node:m_Project->graph.GetNodes()) {
        if (node.kind!=EditorNodeGraph::NodeKind::MultiFrameHdr ||
            node.multiFrameHdr.sourceSetId!=set->sourceSetId ||
            (!set->graphBindingNodeId.empty() && node.instanceUuid!=set->graphBindingNodeId)) continue;
        const NodeEditTarget target{GetProjectDocumentId(),kProjectGraphId,node.instanceUuid};
        const bool changed=!m_Project->nodeEditor || *m_Project->nodeEditor!=target;
        if (changed) m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();
        m_Project->nodeEditor=target;
        m_RawWorkspaceLabUi.activeTool=RawLabTool::MultiFrame;
        m_SelectedRawAdjustmentLayer.clear();
        if (m_RawLayerMaskWorkspace) m_RawLayerMaskWorkspace->layerId=kRawBackgroundId;
        if (changed) ClearRawWorkspaceGraphScopeReadbackCaches();
        return;
    }
}

int EditorModule::ResolveRawCanvasRenderNode(const RenderGraphSnapshot& snapshot,int nodeId) const {
    int compiled = 0; std::string socket;
    ResolveRawCanvasRenderOutput(snapshot, nodeId, {}, compiled, socket);
    return compiled;
}

bool EditorModule::ResolveRawCanvasRenderOutput(const RenderGraphSnapshot& snapshot, int nodeId,
    const std::string& socketId, int& compiledNodeId, std::string& compiledSocketId) const {
    compiledNodeId = 0;
    compiledSocketId.clear();
    if (!m_RawLayerMaskWorkspace) return false;
    const auto& workspace = *m_RawLayerMaskWorkspace;
    if (workspace.projectCanvas && nodeId == workspace.projectCanvas->preparedSource) {
        compiledNodeId = snapshot.rawLayerBackgroundNodeId; compiledSocketId = "imageOut";
        return compiledNodeId > 0;
    }
    if (workspace.projectCanvas && nodeId == workspace.projectCanvas->output) {
        compiledNodeId = snapshot.outputNodeId;
        compiledSocketId = snapshot.outputSocketId.empty() ? "imageOut" : snapshot.outputSocketId;
        return compiledNodeId > 0;
    }
    const auto* node = workspace.graph.FindNode(nodeId);
    if (!node) return false;
    const auto owner = node->rawLayerOwnerId.empty() ? workspace.layerId : node->rawLayerOwnerId;
    const auto port = socketId.empty() ? EditorNodeGraphDefinitions::DefaultOutputSocket(*node) : socketId;
    const auto resolved = ResolveGraphSnapshotOutput(snapshot, {owner, node->instanceUuid, port});
    if (!resolved) return false;
    compiledNodeId = resolved->nodeId; compiledSocketId = resolved->socketId;
    return compiledNodeId > 0;
}
