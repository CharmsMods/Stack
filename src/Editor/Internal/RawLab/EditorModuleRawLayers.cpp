#include "Editor/EditorModule.h"
#include "Editor/RawLayerTools.h"
#include "App/WorkspacePresentation.h"
#include "Project/RawLayerStack.h"
#include "Project/RawLayerSourceTransactions.h"
#include "Project/RawProjectGraph.h"

#include <algorithm>
#include <cstdio>

using namespace Stack::Project;

bool EditorModule::ApplyRawLayerStackEdit(RawLayerStackState candidate, bool affectsPixels, bool completesAction) {
    const auto revision = m_Project->editRevision;
    auto prepared = m_Project->rawLayers.Prepare(std::move(candidate), m_RawLayerStatus,
        &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId(), revision);
    if (!prepared || !m_Project->rawLayers.Commit(std::move(*prepared), m_RawLayerStatus,
        affectsPixels, completesAction, m_Project->editRevision, &m_Project->graph)) return false;
    ReconcileRawNodeEditorSelection();
    MarkDirty();
    if (affectsPixels) {
        MarkRenderRefreshDirty();
        NoteRawWorkspaceRecipePreviewEdit(m_Project->rawLayers.GestureActive());
    }
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
    const bool notesOnly = m_Project->rawLayers.PendingHistoryIsNotesOnly(action);
    const auto* pending = m_Project->rawLayers.PendingSourceHistory(action);
    if (pending && !pending->sourceSetId.empty() && m_Project->snapshot && m_Project->snapshot.use_count() != 1)
        m_Project->snapshot = std::make_shared<RawProjectSnapshot>(*m_Project->snapshot);
    const int sourceId = ResolveRawWorkspaceStageOutputNodeId();
    const auto processingRevision = m_Project->rawLayers.ProcessingRevision();
    const auto update = RestoreRawLayerHistory(m_Project->rawLayers,action,m_Project->rawRecipe,
        m_Project->snapshot.get(),&m_Project->graph,sourceId);
    if (!update.success) { m_RawLayerStatus = update.errorMessage; return false; }
    m_RawLayerStatus.clear();
    if (notesOnly) {
        if (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->projectCanvas) {
            auto& workspace = *m_RawLayerMaskWorkspace;
            RefreshRawProjectGraphNotes(m_Project->rawLayers.State(), *workspace.projectCanvas, workspace.graph);
            workspace.revision = m_Project->rawLayers.Revision();
        }
        MarkDirty(false);
        return true;
    }
    if(m_RawAssetCapture.pending) {
        if(m_RawAssetCapture.cancellation) m_RawAssetCapture.cancellation->store(true);
        m_RawAssetCapture.pending=false;m_RawAssetCapture.application.reset();
        ++m_RawAssetCapture.serial;m_RawAssetCapture.status="Capture canceled by history change.";
    }
    ReconcileRawNodeEditorSelection();
    if (IsEditingRawLayerMaskGraph()) OpenRawLayerMaskGraph(m_RawLayerMaskWorkspace->layerId, false);
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
    MarkDirty();
    if (m_Project->rawLayers.ProcessingRevision() != processingRevision) MarkRenderRefreshDirty();
    return true;
}

void EditorModule::SelectRawAdjustmentLayer(const std::string& id) {
    if (!CommitRawLayerMaskGraph()) return;
    m_Project->rawLayers.EndGesture();
    ResolveRawWorkspaceInteractionDraft(false);
    m_SelectedRawAdjustmentLayer = id;
    if (m_RawLayerMaskWorkspace) {
        m_Project->nodeEditor.reset();
        const auto* owner = FindRawAdjustmentLayer(m_Project->rawLayers.State(),id);
        if (owner) m_RawLayerMaskWorkspace->layerId = owner->id;
    }
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
    const auto* selectedLayer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),id);
    m_Project->nodeEditor.reset();
    if (selectedLayer) {
        const auto remembered=m_Project->layerNodeSelection.find(selectedLayer->id);
        if(remembered!=m_Project->layerNodeSelection.end()) SelectRawNodeEditor(selectedLayer->id,remembered->second);
        if(!m_Project->nodeEditor) {
            // A layer without a remembered selection starts at its first tool,
            // independent of the tool that happened to be open in another layer.
            const auto tools=Stack::Editor::RawLayerToolNodes(selectedLayer->graph);
            if (!tools.empty()) SelectRawNodeEditor(selectedLayer->id,tools.front()->instanceUuid);
        }
    }
    ClearRawWorkspaceGraphScopeReadbackCaches();
    m_RawWorkspaceAnalysisRequested = true;
    MarkRenderRefreshDirty();
}

