#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Editor/NodeEditors/NodeInputAnalysisIdentity.h"
#include "Editor/NodeEditors/NodeInputPresentationCache.h"
#include "Project/RawLayerStack.h"
#include "Project/RawNodeEditing.h"
#include "Project/GraphImageContracts.h"
#include "Raw/RawGraphParameters.h"

using namespace Stack::Project;
using namespace Stack::RawRecipe;

std::optional<GraphOperationKind> EditorModule::SelectedRawOperationKind() const {
    const auto* node = SelectedRawOperation();
    return node ? std::optional<GraphOperationKind>(node->rawOperation.kind) : std::nullopt;
}

const EditorNodeGraph::Node* EditorModule::SelectedRawOperation() const {
    const auto* node = SelectedEditingNode();
    return node && node->kind == EditorNodeGraph::NodeKind::RawOperation ? node : nullptr;
}

void EditorModule::BindNodeInputContext(RawWorkspaceEditContext& context) const {
    context.inputDescriptor={};
    if (!context.nodeTarget || context.nodeTarget->documentId!=GetProjectDocumentId()) return;
    auto& cache=m_NodeEditorState.Get<Stack::Editor::NodeInputPresentationCache>(*context.nodeTarget);
    const Stack::Editor::NodeInputPresentationCache::DescriptorKey key{
        m_Project->editRevision,m_Project->rawLayers.Revision(),GetNodeGraph().GetStructureRevision(),
        m_RenderRevision,CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame,m_TimelineUi.liveEditPreviewFrame};
    const bool transientDraft=(m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->dirty) ||
        !m_TimelineUi.liveEditPreviewTargets.empty();
    if (cache.descriptorValid && !transientDraft && cache.descriptorKey==key) {
        context.inputDescriptor=cache.descriptor;
    } else {
        const auto snapshot=BuildGraphSnapshot();
        const auto compiled=std::find_if(snapshot.nodes.begin(),snapshot.nodes.end(),[&](const auto& node) {
            return node.authoredNodeUuid==context.nodeTarget->nodeUuid && node.authoredGraphId==context.nodeTarget->layerId;
        });
        if (compiled!=snapshot.nodes.end()) {
            const auto input=std::find_if(snapshot.links.begin(),snapshot.links.end(),[&](const auto& link) {
                return link.toNodeId==compiled->nodeId && link.toSocketId=="imageIn";
            });
            if (input!=snapshot.links.end()) context.inputDescriptor=input->semanticDescriptor;
        }
        cache.descriptorKey=key;cache.descriptor=context.inputDescriptor;
        cache.descriptorValid=!transientDraft;
    }
    if (const auto space=RawOperationInputWorkingSpace(context.inputDescriptor)) context.recipe.technical.workingSpace=*space;
}

void EditorModule::BindRawOperationContext(RawWorkspaceEditContext& context) const {
    if (const auto* selected=SelectedEditingNode()) context.presentationNodeId=selected->id;
    const auto describeCurveInput=[&] {
        const auto* selected=SelectedEditingNode();
        if (!selected || !context.nodeTarget) return;
        BindNodeInputContext(context);
        if (selected->kind!=EditorNodeGraph::NodeKind::RawOperation ||
            selected->rawOperation.kind!=GraphOperationKind::ToneCurve) return;
        if (m_RawWorkspaceGraphScopeReadback.valid &&
            m_RawWorkspaceGraphScopeReadback.stage==RawDevelopmentGraphScopeStage::FinishToneInput &&
            m_RawWorkspaceGraphScopeReadbackRevision==m_RawWorkspaceFinishToneInputGraphScopeCache.contentRevision &&
            m_RawWorkspaceFinishToneInputGraphScopeCache.sourceKey==context.nodeTarget->Key() &&
            m_RawWorkspaceFinishToneInputGraphScopeCache.inputFingerprint==Stack::Editor::NodeInputAnalysisIdentity(
                *context.nodeTarget,m_Project->editRevision,CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame)) {
            const auto& parameters=selected->rawOperation.parameters;
            const auto mode=parameters.value("curveMode",std::string("components"));
            auto& cache=m_NodeEditorState.Get<Stack::Editor::NodeInputPresentationCache>(*context.nodeTarget);
            const bool components=mode=="components";
            const int domain=components?parameters.value("domain",0):1;
            const float minimum=components?parameters.value("logMinEv",-10.f):-16.f;
            const float maximum=components?parameters.value("logMaxEv",6.f):16.f;
            const float grey=parameters.value("middleGrey",.18f);
            const Stack::Editor::NodeInputPresentationCache::HistogramKey key{
                m_RawWorkspaceGraphScopeReadbackRevision,components,domain,minimum,maximum,grey};
            if (!cache.histogramValid || cache.histogramKey!=key ||
                !(cache.histogramDescriptor==context.inputDescriptor)) {
                cache.histogram=Stack::Editor::RawLabInternal::BuildNodeInputToneHistogram(
                    m_RawWorkspaceGraphScopeReadback,Stack::NodeMath::DescribeImageOperationInput(context.inputDescriptor),
                    components,domain,minimum,maximum,grey);
                cache.histogramKey=key;cache.histogramDescriptor=context.inputDescriptor;
                cache.histogramValid=m_RawWorkspaceGraphScopeReadbackRevision!=0;
                ++cache.histogramRevision;
            }
            context.inputHistogram=cache.histogram;
            context.inputHistogramRevision=cache.histogramRevision;
        }
    };
    if (m_Project->nodeEditor && m_Project->nodeEditor->layerId == kProjectGraphId) {
        const auto* node = SelectedEditingNode();
        if (!node) { context.canEdit=false; context.error="The selected node is no longer available."; return; }
        context.nodeTarget = m_Project->nodeEditor;
        context.nodeRevision = m_Project->editRevision;
        context.rawAdjustmentLayerId = kProjectGraphId;
        context.rawOperationUuid = node->instanceUuid;
        if (node->kind==EditorNodeGraph::NodeKind::MultiFrameHdr && node->multiFrameHdr.bracketingSource) {
            // This source edits its canonical source-set recipe through the
            // Bracketing coordinator. It has no graph-parameter draft or input
            // analysis to copy/prepare while the ordinary View is submitted.
            return;
        }
        if (node->kind != EditorNodeGraph::NodeKind::RawOperation) context.nodeDraft = *node;
        if (node->kind == EditorNodeGraph::NodeKind::Layer)
            context.nodeSettings = node->layerParameters;
        if (node->kind == EditorNodeGraph::NodeKind::RawOperation)
            context.recipe = m_RawOperationRecipeCache.Read(node->rawOperation,
                node->instanceUuid, GetProjectDocumentId(), m_Project->editRevision);
        for (const auto& link : m_Project->graph.GetLinks()) if (link.toNodeId == node->id) {
            if (link.toSocketId.rfind("param:",0)==0) context.drivenParameters.insert(link.toSocketId.substr(6));
            if (const auto* producer=m_Project->graph.FindNode(link.fromNodeId))
                context.inputs.emplace(link.toSocketId,Stack::GraphModel::Endpoint{kProjectGraphId,producer->instanceUuid,link.fromSocketId});
        }
        BindUniformParameterContext(context,*node);
        describeCurveInput();
        return;
    }
    if (const auto* node = SelectedEditingNode()) {
        const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
        context.nodeTarget = NodeEditTarget{GetProjectDocumentId(),layer->id,node->instanceUuid};
        context.nodeRevision = m_Project->rawLayers.Revision();
        context.rawAdjustmentLayerId = layer->id;
        context.rawOperationUuid = node->instanceUuid;
        context.persistedRecipe = nullptr;
        for (const auto& link:layer->graph.GetLinks()) if(link.toNodeId==node->id) {
            if(link.toSocketId.rfind("param:",0)==0)
                context.drivenParameters.insert(link.toSocketId.substr(6));
            const auto input=RawNodeInput(m_Project->rawLayers.State(),*context.nodeTarget,GetProjectDocumentId(),link.toSocketId);
            if(input) context.inputs.emplace(link.toSocketId,*input);
        }
        const auto& editor = Stack::Editor::FindNodeEditor(*node);
        const bool editsNodeDraft = editor.primary || editor.settings || editor.reset || editor.overlay;
        if(node->kind!=EditorNodeGraph::NodeKind::RawOperation && editsNodeDraft) {
            context.nodeDraft=*node;
            if(node->kind==EditorNodeGraph::NodeKind::Layer)
                context.nodeSettings=node->layerParameters;
        }
    } else if (m_Project->nodeEditor) {
        context.canEdit = false;
        context.error = "The selected node is no longer available.";
        return;
    }
    if (!SelectedRawOperationKind()) {
        if (const auto* node=SelectedEditingNode()) BindUniformParameterContext(context,*node);
        describeCurveInput();
        if (!m_SelectedRawAdjustmentLayer.empty() && m_SelectedRawAdjustmentLayer != kRawBackgroundId &&
            (m_RawWorkspaceLabUi.activeTool == RawLabTool::Light || m_RawWorkspaceLabUi.activeTool == RawLabTool::Denoise ||
             m_RawWorkspaceLabUi.activeTool == RawLabTool::RgbDenoise)) {
            context.canEdit = false;
            context.error = "Select Background to edit camera preparation.";
        }
        return;
    }
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!layer) return;
    context.rawAdjustmentLayerId = layer->id;
    context.persistedRecipe = nullptr;
    const auto* operation = SelectedRawOperation();
    if (!operation) {
        context.canEdit = false;
        context.error = "This operation is absent. Add an operation or select another instance.";
        return;
    }
    context.rawOperationUuid = operation->instanceUuid;
    auto editing = m_RawOperationRecipeCache.Read(*layer, operation->instanceUuid,
        GetProjectDocumentId(), m_Project->rawLayers.Revision());
    editing.source = context.recipe.source;
    editing.technical.workingSpace = context.recipe.technical.workingSpace;
    editing.technical.processingVersion = context.recipe.technical.processingVersion;
    editing.cropRotation = context.recipe.cropRotation;
    context.recipe = std::move(editing);
    BindUniformParameterContext(context,*operation);
    describeCurveInput();
}

void EditorModule::RenderRawOperationInstancePicker(RawWorkspaceEditContext& context) {
    const auto kind = SelectedRawOperationKind();
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!kind || !layer) return;
    struct Choice { std::string uuid, title; bool bypass; bool connected; };
    std::vector<Choice> choices;
    for (const auto& node : layer->graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawOperation && node.rawOperation.kind == *kind)
            choices.push_back({node.instanceUuid, node.title, !node.rawOperation.enabled,
                layer->graph.FindInputLink(node.id, "imageIn") != nullptr});
    }
    const std::string key = layer->id + "/" + GraphOperationId(*kind);
    const auto* current = SelectedRawOperation();
    if (!current || choices.size() > 1) {
        ImGui::TextUnformatted("Instance");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##RawOperationInstance", current ? current->title.c_str() : "Select an instance")) {
            int index = 0;
            for (const auto& choice : choices) {
                ImGui::PushID(choice.uuid.c_str());
                const auto label = choice.title + " " + std::to_string(++index);
                if (ImGui::Selectable(label.c_str(), choice.uuid == context.rawOperationUuid)) {
                    SelectRawNodeEditor(layer->id, choice.uuid);
                    BeginRawWorkspaceEditContext(context.source, context);
                    ClearRawWorkspaceGraphScopeReadbackCaches();
                    m_RawWorkspaceAnalysisPending = true;
                    m_RawWorkspaceAnalysisRequested = false;
                    m_RawWorkspaceAnalysisScopeRetryCount = 0;
                    m_RawWorkspaceAnalysisQuietUntilTime = ImGui::GetTime() + 0.25;
                    MarkRenderRefreshDirty();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    current = SelectedRawOperation();
    if (current) {
        for (const auto& parameter : GraphParameters(current->rawOperation)) {
            const auto* link = layer->graph.FindInputLink(current->id, "param:" + parameter.id);
            if (!link) continue;
            const auto* source = layer->graph.FindNode(link->fromNodeId);
            ImGui::TextDisabled("%s is driven by %s", parameter.label.c_str(), source ? source->title.c_str() : "an unresolved node");
        }
        if (!current->rawOperation.enabled) ImGui::TextDisabled("Bypassed. These settings are retained.");
        if (!layer->graph.FindInputLink(current->id, "imageIn")) ImGui::TextDisabled("Image input is disconnected.");
    } else if (ImGui::Button("Add before Layer result")) {
        auto candidate = m_Project->rawLayers.State();
        auto* destination = FindRawAdjustmentLayer(candidate, layer->id);
        const auto* result = FindRawRole(*destination, Stack::GraphModel::NodeRole::LayerResult);
        const int resultId = result->id;
        const auto* incoming = destination->graph.FindInputLink(resultId, "imageIn");
        const std::optional<EditorNodeGraph::Link> old = incoming ? std::optional<EditorNodeGraph::Link>(*incoming) : std::nullopt;
        auto* added = destination->graph.AddRawOperationNode(*kind, {1800, 180});
        const auto uuid = added->instanceUuid;
        const int addedId = added->id;
        bool valid = true;
        if (old) valid = destination->graph.TryConnectSockets(old->fromNodeId, old->fromSocketId, addedId, "imageIn", &m_RawLayerStatus);
        if (valid) valid = destination->graph.TryConnectSockets(addedId, "imageOut", resultId, "imageIn", &m_RawLayerStatus);
        if (valid && ApplyRawLayerStackEdit(std::move(candidate))) {
            SelectRawNodeEditor(layer->id, uuid);
            BeginRawWorkspaceEditContext(context.source, context);
        }
    }
}

bool EditorModule::RawStartingPointGraphSupported(std::string& reason) const {
    const auto& state = m_Project->rawLayers.State();
    const auto& layer = state.background;
    const auto unavailable = [&]() {
        reason = "Starting Point needs the default Background tool order, with one active instance per tool and no graph-driven parameters or tool masks. Edit this graph with the ordinary controls.";
        return false;
    };
    if (!layer.enabled || layer.opacity != 1.f || layer.layerMask || !layer.animation.tracks.empty()) return unavailable();
    for (const auto& other : state.layers) if (other.enabled && other.opacity != 0.f) return unavailable();
    const auto* result = FindRawRole(layer,Stack::GraphModel::NodeRole::LayerResult);
    if (!result) return unavailable();
    int next = result->id;
    for (int i=static_cast<int>(GraphOperationKind::Count)-1;i>=0;--i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        const auto* input = layer.graph.FindInputLink(next,"imageIn");
        const auto* node = input ? layer.graph.FindNode(input->fromNodeId) : nullptr;
        if (!node || node->kind != EditorNodeGraph::NodeKind::RawOperation || node->rawOperation.kind != kind ||
            !node->rawOperation.enabled || input->fromSocketId != "imageOut") return unavailable();
        const auto selected = m_Project->rawOperationSelection.find(layer.id + "/" + GraphOperationId(kind));
        const auto* edited = FindRawOperation(layer,kind,
            selected == m_Project->rawOperationSelection.end() ? std::string{} : selected->second);
        if (!edited || edited->instanceUuid != node->instanceUuid) return unavailable();
        for (const auto& connection : layer.graph.GetLinks()) {
            if (connection.toNodeId != node->id || connection.toSocketId == "imageIn") continue;
            // Direct graph-owned coverage has the same additive mathematics
            // in the recipe evaluator. Processed or referenced coverage does not.
            const auto* coverage = layer.graph.FindNode(connection.fromNodeId);
            if ((connection.toSocketId.rfind("gradient:",0) != 0 && connection.toSocketId.rfind("area:",0) != 0) ||
                !coverage || coverage->kind != EditorNodeGraph::NodeKind::MaskGenerator ||
                connection.fromSocketId != "maskOut" ||
                coverage->role != Stack::GraphModel::NodeRole::Ordinary || coverage->rawCoverage.empty()) return unavailable();
        }
        next = node->id;
    }
    const auto* input = layer.graph.FindInputLink(next,"imageIn");
    const auto* current = FindRawRole(layer,Stack::GraphModel::NodeRole::CurrentImage);
    if (!input || !current || input->fromNodeId != current->id) return unavailable();
    reason.clear(); return true;
}

RawDevelopmentRecipe EditorModule::ReadRawControlRecipe() const {
    auto result = BuildWorkspaceSourceRecipe(m_Project->rawRecipe);
    const auto& layer = m_Project->rawLayers.State().background;
    for (int i=0; i<static_cast<int>(GraphOperationKind::Count); ++i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        const auto selection = m_Project->rawOperationSelection.find(layer.id + "/" + GraphOperationId(kind));
        const auto* node = FindRawOperation(layer, kind,
            selection == m_Project->rawOperationSelection.end() ? std::string{} : selection->second);
        if (!node) continue;
        const auto part = ReadRawLayerOperation(layer, node->instanceUuid);
        switch (kind) {
            case GraphOperationKind::Calibration: result.colorCalibration = part.colorCalibration; break;
            case GraphOperationKind::Exposure: result.preToneExposureEv = part.preToneExposureEv; break;
            case GraphOperationKind::LocalEv: result.localRange = part.localRange; result.evGradients = part.evGradients; break;
            case GraphOperationKind::ToneCurve: result.finishTone=part.finishTone; break;
            case GraphOperationKind::ColorWarp: result.colorWarp = part.colorWarp; break;
            case GraphOperationKind::DetailContrast: result.detailContrast = part.detailContrast; break;
            default: break;
        }
    }
    return result;
}

bool EditorModule::PrepareRawControlRecipeEdit(const RawDevelopmentRecipe& recipe,
    RawLayerStackState& candidate, bool& changed, std::string& error) const {
    changed = false;
    const auto previous = ReadRawControlRecipe();
    candidate = m_Project->rawLayers.State();
    for (int i=0; i<static_cast<int>(GraphOperationKind::Count); ++i) {
        const auto kind = static_cast<GraphOperationKind>(i);
        auto before = MakeGraphOperation(kind), after = before;
        WriteGraphOperation(before, previous); WriteGraphOperation(after, recipe);
        // A whole-recipe exposure edit belongs to Exposure. Local EV's vertical
        // gesture owns a separate offset on that particular curve instance.
        if (kind == GraphOperationKind::LocalEv) { before.parameters.erase("ev"); after.parameters.erase("ev"); }
        if (before.parameters == after.parameters) continue;
        auto& layer = candidate.background;
        const auto selection = m_Project->rawOperationSelection.find(layer.id + "/" + GraphOperationId(kind));
        const auto* node = FindRawOperation(layer, kind,
            selection == m_Project->rawOperationSelection.end() ? std::string{} : selection->second);
        if (!node) {
            const auto* end = FindRawRole(layer,Stack::GraphModel::NodeRole::LayerResult);
            const int endId=end->id;
            const auto* input=layer.graph.FindInputLink(endId,"imageIn");
            if (!input) { error="Connect the Background chain before applying this adjustment."; return false; }
            const auto from=*input;
            node=layer.graph.AddRawOperationNode(kind,{end->position.x,180.f});
            const int id=node->id;
            if (!layer.graph.TryConnectSockets(from.fromNodeId,from.fromSocketId,id,"imageIn",&error) ||
                !layer.graph.TryConnectSockets(id,"imageOut",endId,"imageIn",&error)) return false;
        }
        auto edited=recipe;
        if (kind == GraphOperationKind::LocalEv) edited.preToneExposureEv=ReadGraphOperation(node->rawOperation).preToneExposureEv;
        if (!WriteRawLayerOperation(layer, node->instanceUuid, edited, error)) return false;
        changed = true;
    }
    return ValidateRawLayerStack(candidate, error, &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId());
}
