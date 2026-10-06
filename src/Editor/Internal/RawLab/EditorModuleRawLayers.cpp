#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Project/RawLayerStack.h"
#include "Project/RawLayerSourceTransactions.h"

#include <algorithm>
#include <cstdio>

using namespace Stack::Project;

bool EditorModule::ApplyRawLayerStackEdit(RawLayerStackState candidate) {
    if (!ValidateRawLayerStack(candidate, m_RawLayerStatus, &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId())) return false;
    if (!m_Project->rawLayers.Apply(std::move(candidate), m_RawLayerStatus)) return false;
    MarkDirty();
    MarkRenderRefreshDirty();
    NoteRawWorkspaceRecipePreviewEdit(m_Project->rawLayers.GestureActive());
    return true;
}

bool EditorModule::UndoRawLayerEdit() {
    return RestoreRawLayerHistoryEdit(false);
}
bool EditorModule::RedoRawLayerEdit() {
    return RestoreRawLayerHistoryEdit(true);
}
bool EditorModule::RestoreRawLayerHistoryEdit(bool redo) {
    if (!CommitRawLayerMaskGraph()) return false;
    m_Project->rawLayers.EndGesture();
    const auto action = redo ? RawLayerHistoryAction::Redo : RawLayerHistoryAction::Undo;
    const auto* pending = m_Project->rawLayers.PendingSourceHistory(action);
    if (pending && !pending->sourceSetId.empty() && m_Project->snapshot && m_Project->snapshot.use_count() != 1)
        m_Project->snapshot = std::make_shared<RawProjectSnapshot>(*m_Project->snapshot);
    const int sourceId = ResolveRawWorkspaceStageOutputNodeId();
    const auto update = RestoreRawLayerHistory(m_Project->rawLayers,action,m_Project->rawRecipe,
        m_Project->snapshot.get(),&m_Project->graph,sourceId);
    if (!update.success) { m_RawLayerStatus = update.errorMessage; return false; }
    m_RawLayerStatus.clear();
    if (IsEditingRawLayerMaskGraph()) OpenRawLayerMaskGraph(m_RawLayerMaskWorkspace->layerId);
    if (update.preMergeChanged) {
        m_MfdAdoptedRawResult.reset();
        for (auto& node : m_Project->graph.EditNodes())
            if (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise && node.multiFrameDenoise.sourceSetId == update.sourceSetId) {
                node.multiFrameDenoise.resultState = "unavailable";
                node.multiFrameDenoise.presentationStatus = "Source settings changed; process the burst again.";
            }
    }
    if (update.preMergeChanged || update.postMergeChanged) {
        if (update.sourceSetId.empty())
            if (auto* node = m_Project->graph.FindNode(sourceId)) node->rawDevelopment.recipe = m_Project->rawRecipe;
        MarkRenderDirty(sourceId);
        return true;
    }
    MarkDirty(); MarkRenderRefreshDirty(); return true;
}

void EditorModule::SelectRawAdjustmentLayer(const std::string& id) {
    if (!CommitRawLayerMaskGraph()) return;
    m_Project->rawLayers.EndGesture();
    ResolveRawWorkspaceInteractionDraft(false);
    m_SelectedRawAdjustmentLayer = id;
    m_EditingRawLayerMask.reset();
    m_RawLayerMaskGenerator = -1;
    m_RawLayerMaskDrag = -1;
    m_RawWorkspaceLabUi.selectedToneGradient = -1;
    m_RawWorkspaceLabUi.selectedEvGradient = -1;
    m_RawWorkspaceLabUi.selectedTonePoint = -1;
    m_RawWorkspaceLabUi.sceneToneSelectedPoint = -1;
    m_RawWorkspaceLabUi.sceneToneDraggingPoint = -1;
    if (m_RawWorkspaceLabUi.zoneAreas.overlayTexture) glDeleteTextures(1, &m_RawWorkspaceLabUi.zoneAreas.overlayTexture);
    m_RawWorkspaceLabUi.zoneAreas = {};
    m_RawWorkspaceLabUi.toneCurveGraphs = {};
    m_RawWorkspaceLabUi.gradientDrawShape = -1;
    m_RawWorkspaceLabUi.gradientDragHandle = -1;
    if (!id.empty() && Stack::EditorModuleTypes::RawLabDrawerTool(m_RawWorkspaceLabUi.activeTool) != RawLabTool::Zones)
        m_RawWorkspaceLabUi.activeTool = RawLabTool::Tone;
    ClearRawWorkspaceGraphScopeReadbackCaches();
    m_RawWorkspaceAnalysisRequested = true;
    MarkRenderRefreshDirty();
}

void EditorModule::RenderRawLayerMaskAttachment(const char* label, bool wholeLayer) {
    const auto* selected = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    if (!selected) return;
    const auto* operation = SelectedRawOperation();
    if (!wholeLayer && !operation) return;
    const std::string operationUuid = operation ? operation->instanceUuid : std::string{};
    const auto attached = wholeLayer ? selected->layerMask :
        GetRawOperationMask(m_Project->rawLayers.State(), *selected, operationUuid);
    const bool linked = !wholeLayer && selected->graph.FindInputLink(operation->id, "maskIn");
    const auto* output = attached ? FindRawMaskOutput(m_Project->rawLayers.State(), *attached) : nullptr;
    const std::string preview = output ? output->name : linked ? "Graph mask" : attached ? "Unresolved mask" : "Entire image";
    const bool empty = output && [&] {
        const auto* owner = FindRawAdjustmentLayer(m_Project->rawLayers.State(), attached->layerId);
        const auto connected = owner->graph.GetConnectedOutputNodeIds();
        const auto* producer = FindRawPublishedNode(*owner, *output);
        return !producer || std::find(connected.begin(), connected.end(), producer->id) == connected.end();
    }();
    ImGui::PushID(label);
    if (attached) {
        const bool active = m_EditingRawLayerMask && m_EditingRawLayerMask->layerId == attached->layerId &&
            m_EditingRawLayerMask->outputId == attached->outputId;
        if (RenderRawLayerThumbnail(attached->layerId, attached->outputId, ImVec2(36,36), active)) {
            m_EditingRawLayerMask = attached;
            m_RawLayerMaskGenerator = -1;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenRawLayerMaskGraph(attached->layerId);
        }
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(label);
    if (empty) ImGui::TextDisabled("Mask is empty. This adjustment is not applied.");
    const auto assign = [&](std::optional<RawMaskReference> mask) {
        auto candidate = m_Project->rawLayers.State();
        auto* layer = FindRawAdjustmentLayer(candidate, m_SelectedRawAdjustmentLayer);
        if (wholeLayer) layer->layerMask = std::move(mask);
        else if (!SetRawOperationMask(candidate, layer->id, operationUuid, mask, m_RawLayerStatus)) return;
        ApplyRawLayerStackEdit(std::move(candidate));
    };
    if (ImGui::BeginCombo("##mask", preview.c_str())) {
        if (ImGui::Selectable("Entire image", !attached && !linked)) assign(std::nullopt);
        // Copy choices because selection commits a new document state.
        struct Choice { RawMaskReference reference; std::string label; };
        std::vector<Choice> choices;
        for (const auto* layer : RawLayersInOrder(m_Project->rawLayers.State()))
            for (const auto& mask : layer->maskOutputs)
                choices.push_back({{layer->id, mask.id}, layer->name + " / " + mask.name});
        for (const auto& choice : choices) {
            ImGui::PushID(choice.reference.outputId.c_str());
            if (ImGui::Selectable(choice.label.c_str())) assign(choice.reference);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (wholeLayer && ImGui::Button("Add mask")) ImGui::OpenPopup("new-mask");
    if (wholeLayer && ImGui::BeginPopup("new-mask")) {
        for (auto kind : {EditorNodeGraph::MaskGeneratorKind::RadialGradient,
                         EditorNodeGraph::MaskGeneratorKind::LinearGradient,
                         EditorNodeGraph::MaskGeneratorKind::Square}) {
            const char* name = kind == EditorNodeGraph::MaskGeneratorKind::RadialGradient ? "Radial" :
                kind == EditorNodeGraph::MaskGeneratorKind::LinearGradient ? "Linear" : "Custom mask";
            if (ImGui::MenuItem(name)) {
                CreateRawLayerMask(m_SelectedRawAdjustmentLayer, {}, kind, m_RawLayerStatus);
            }
        }
        ImGui::EndPopup();
    }
    if (attached) {
        if (wholeLayer) ImGui::SameLine();
        if (ImGui::Button("Edit shape")) {
            m_EditingRawLayerMask = attached;
            m_RawLayerMaskGenerator = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Graph")) OpenRawLayerMaskGraph(attached->layerId);
    }
    ImGui::PopID();
}

