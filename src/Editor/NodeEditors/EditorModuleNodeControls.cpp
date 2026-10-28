#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Editor/Timeline/TimelineParameterEdits.h"
#include "imgui_internal.h"
#include "Editor/LayerRegistry.h"

bool EditorModule::RenderLayerControlsWithDirtyTracking(
    EditorNodeGraph::Node& node,
    const std::function<void(LayerBase&)>& renderControls,
    const std::function<void(std::function<void()>)>& deferCommand) {
    if (m_NodeEditorDrawingContext && m_NodeEditorDrawingContext->nodeTarget &&
        m_NodeEditorDrawingContext->nodeDraft && m_NodeEditorDrawingContext->nodeDraft->instanceUuid==node.instanceUuid) {
        auto& context=*m_NodeEditorDrawingContext;
        struct Adapter { std::shared_ptr<LayerBase> layer; std::uint64_t revision=~std::uint64_t{0}; };
        auto& adapter=context.State<Adapter>();
        if (!adapter.layer) {
            adapter.layer=LayerRegistry::CreateLayer(node.layerType);
            if (adapter.layer) adapter.layer->InitializeGL();
        }
        if (!adapter.layer) {ImGui::TextDisabled("This operation is unavailable.");return false;}
        if (adapter.revision!=context.nodeRevision) {
            Stack::GraphModel::PrepareLayerParameterDraft(*adapter.layer,context.nodeSettings,node.title);
            adapter.revision=context.nodeRevision;
        }
        auto& gui=*ImGui::GetCurrentContext();
        const bool input=gui.ActiveId || gui.ActiveIdPreviousFrame || gui.NavActivateId || gui.NavMoveSubmitted ||
            ImGui::GetIO().MouseWheel!=0.f || ImGui::GetIO().MouseWheelH!=0.f ||
            ImGui::IsMouseClicked(0) || ImGui::IsMouseReleased(0);
        // Legacy controls cannot manually replace graph-driven payload fields.
        ImGui::BeginDisabled(!context.drivenParameters.empty());
        renderControls(*adapter.layer);
        ImGui::EndDisabled();
        if (!input) return false;
        const auto after=Stack::GraphModel::CaptureLayerParameters(*adapter.layer);
        if (after==context.nodeSettings || !context.drivenParameters.empty()) return false;
        context.nodeSettings=after;
        context.draftChanged=true;
        return true;
    }
    if (node.kind != EditorNodeGraph::NodeKind::Layer ||
        node.layerIndex < 0 ||
        node.layerIndex >= static_cast<int>(GetLayers().size()) ||
        !GetLayers()[node.layerIndex]) {
        return false;
    }

    LayerBase& layer = *GetLayers()[node.layerIndex];
    Stack::GraphModel::PrepareLayerParameterDraft(layer,node.layerParameters,node.title);
    const auto& editor=Stack::Editor::FindNodeEditor(node);
    if (editor.primary) {
        // Graph and View share the instance editor and action. The drawing
        // target is explicit: drawing another card never changes selection.
        RawWorkspaceEditContext context;
        const auto owner=IsEditingRawLayerMaskGraph() ? node.rawLayerOwnerId.empty()
            ? m_RawLayerMaskWorkspace->layerId : node.rawLayerOwnerId : Stack::Project::kProjectGraphId;
        context.nodeTarget=Stack::Project::NodeEditTarget{GetProjectDocumentId(),owner,node.instanceUuid};
        context.nodeRevision=IsEditingRawLayerMaskGraph() ? m_Project->rawLayers.Revision() : m_Project->editRevision;
        context.rawAdjustmentLayerId=owner; context.rawOperationUuid=node.instanceUuid;
        context.nodeDraft=node; context.nodeSettings=node.layerParameters;
        context.editorState=&m_NodeEditorState;
        using Phase=Stack::Project::ProjectLifecyclePhase;
        const auto phase=m_Project->lifecycle.Phase();
        context.canEdit=phase!=Phase::Loading && phase!=Phase::Importing &&
            phase!=Phase::Conflict && phase!=Phase::ReadOnlyRecovery;
        for (const auto& link:GetNodeGraph().GetLinks())
            if (link.toNodeId==node.id && link.toSocketId.rfind("param:",0)==0)
                context.drivenParameters.insert(link.toSocketId.substr(6));
        context.presentationNodeId=node.id;
        BindNodeInputContext(context);
        BindUniformParameterContext(context,node);
        ImGui::BeginDisabled(!context.canEdit);
        const bool changed=editor.primary(*this,context);
        ImGui::EndDisabled();
        const bool active=ImGui::IsAnyItemActive();
        if (deferCommand && (changed || ImGui::IsKeyPressed(ImGuiKey_Escape,false))) {
            context.editorState=nullptr;
            deferCommand([this,context=std::move(context),changed,active]() mutable {
                CommitRawWorkspaceEditContext(context,changed,active);
            });
        } else if (changed || ImGui::IsKeyPressed(ImGuiKey_Escape,false))
            CommitRawWorkspaceEditContext(context,changed,active);
        return changed;
    }
    const auto editContext=GetGraphEditorContext();
    auto animation=*editContext.animation;
    const bool editsTimeline=m_TimelineUi.open && !animation.tracks.empty() && !IsEditingRawLayerMaskGraph();
    const int editFrame=m_TimelineUi.currentFrame;
    const auto instanceUuid=node.instanceUuid;
    const nlohmann::json before = Stack::GraphModel::CaptureLayerParameters(layer);
    const bool beforeEnabled = layer.IsEnabled();
    const bool beforeVisible = layer.IsVisible();
    renderControls(layer);
    const nlohmann::json after = Stack::GraphModel::CaptureLayerParameters(layer);
    if (before != after ||
        beforeEnabled != layer.IsEnabled() ||
        beforeVisible != layer.IsVisible()) {
        auto timelineEdit=editsTimeline ? Stack::Timeline::PrepareParameterEditAnimation(
            animation,node,editContext.graphId,editFrame,before,after) : Stack::Timeline::ParameterEditAnimation{std::move(animation),{}};
        auto proposal=Stack::GraphModel::ProposeEdit(*editContext.graph,editContext.revision,[&](auto& graph) {
                const auto found=std::find_if(graph.EditNodes().begin(),graph.EditNodes().end(),[&](const auto& candidate) {
                    return candidate.instanceUuid==instanceUuid;
                });
                if (found==graph.EditNodes().end()) throw std::runtime_error("The edited effect is no longer available.");
                found->layerParameters=after;
            });
        auto settings=Stack::GraphModel::LayerParameterRecords(proposal.candidate);
        auto commit=[this,editContext,proposal=std::move(proposal),settings=std::move(settings),timelineEdit=std::move(timelineEdit)]() mutable {
            std::string error;
            if (!editContext.applyDocumentEdit(std::move(proposal),std::move(settings),std::move(timelineEdit.animation),error))
                PostNotification(UiNotificationSeverity::Error,error,"graph-effect-parameters");
            else for (const auto& target:timelineEdit.livePreviewTargets) AddTimelineLiveEditPreviewTarget(target);
        };
        if (deferCommand) deferCommand(std::move(commit)); else commit();
        return true;
    }
    return false;
}

