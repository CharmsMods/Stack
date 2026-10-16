#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/NodeEditors/ToneCurveEditorState.h"
#include "Editor/EditorModule.h"
#include "Editor/RawLayerTools.h"
#include "Project/RawNodeEditing.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "imgui_internal.h"

using namespace Stack::Project;

const EditorNodeGraph::Node* EditorModule::SelectedEditingNode() const {
    if (m_Project->nodeEditor && m_Project->nodeEditor->layerId == kProjectGraphId) {
        if (m_Project->nodeEditor->documentId != GetProjectDocumentId()) return nullptr;
        for (const auto& node : m_Project->graph.GetNodes())
            if (node.instanceUuid == m_Project->nodeEditor->nodeUuid) return &node;
        return nullptr;
    }
    if(m_Project->nodeEditor)
        return ResolveRawNode(m_Project->rawLayers.State(),*m_Project->nodeEditor,GetProjectDocumentId());
    if (!IsRawWorkspaceProjectActive() && !IsMultiFrameRawProjectActive()) return nullptr;
    // Compatibility for source startup and old grouped-tool entry points.
    const auto* editor=Stack::Editor::FindNodeEditorForTool(m_RawWorkspaceLabUi.activeTool,m_RawWorkspaceLabUi.sceneToneView);
    const auto* layer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    if(!editor || !layer) return nullptr;
    for(const auto& node:layer->graph.GetNodes()) if(node.definitionId==editor->definitionId) {
        if(node.kind!=EditorNodeGraph::NodeKind::RawOperation) return &node;
        const auto key=layer->id+"/"+Stack::RawRecipe::GraphOperationId(node.rawOperation.kind);
        const auto selected=m_Project->rawOperationSelection.find(key);
        return FindRawOperation(*layer,node.rawOperation.kind,
            selected==m_Project->rawOperationSelection.end()?std::string{}:selected->second);
    }
    return nullptr;
}

const Stack::Editor::NodeEditorDefinition* EditorModule::SelectedNodeEditor() const {
    const auto* node=SelectedEditingNode();
    return node?&Stack::Editor::FindNodeEditor(*node):nullptr;
}

RawDevelopmentGraphScopeStage EditorModule::ActiveNodeEditorAnalysis() const {
    const auto* editor=SelectedNodeEditor();
    if(!editor && !m_Project->nodeEditor)
        editor=Stack::Editor::FindNodeEditorForTool(m_RawWorkspaceLabUi.activeTool,m_RawWorkspaceLabUi.sceneToneView);
    return editor?editor->inputAnalysis:RawDevelopmentGraphScopeStage::None;
}

std::string EditorModule::RawNodeEditorStateKey() const {
    if (m_Project->nodeEditor) return m_Project->nodeEditor->Key();
    const auto* node=SelectedEditingNode();
    const auto* layer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    return node && layer ? NodeEditTarget{GetProjectDocumentId(),layer->id,node->instanceUuid}.Key() :
        GetProjectDocumentId()+"/source/"+std::to_string(static_cast<int>(m_RawWorkspaceLabUi.activeTool));
}

void EditorModule::SelectRawNodeEditor(const std::string& layerId,const std::string& uuid) {
    const NodeEditTarget target{GetProjectDocumentId(),layerId,uuid};
    const auto* node=ResolveRawNode(m_Project->rawLayers.State(),target,GetProjectDocumentId());
    if(!node || node->role!=Stack::GraphModel::NodeRole::Ordinary) return;
    const bool changed=!m_Project->nodeEditor || *m_Project->nodeEditor!=target;
    if(changed) {
        if(m_Project->nodeEditor) {
            auto& curve=m_NodeEditorState.Get<Stack::Editor::RawLabInternal::CurveCanvasState>(*m_Project->nodeEditor);
            if(curve.activeTonalSlider && ImGui::GetActiveID()==curve.activeTonalSlider) ImGui::ClearActiveID();
            curve.gesture=curve.exposureGesture=curve.amountGesture=curve.normalizationGesture=curve.tonalSliderGesture=false;
            curve.activeTonalSlider=0;curve.probe.reset();
            auto& tone=m_NodeEditorState.Get<Stack::Editor::ToneCurveEditorState>(*m_Project->nodeEditor);
            for (auto& canvas:tone.canvas) {
                if(canvas.activeTonalSlider && ImGui::GetActiveID()==canvas.activeTonalSlider) ImGui::ClearActiveID();
                canvas.gesture=canvas.exposureGesture=canvas.amountGesture=canvas.normalizationGesture=canvas.tonalSliderGesture=false;
                canvas.activeTonalSlider=0;canvas.probe.reset();
            }
            if (tone.advanced) {tone.advanced->ClearViewportProbe();tone.advanced->EndViewportTargetDrag();}
        }
        m_Project->rawLayers.EndGesture();
        if(m_RawWorkspaceAdaptiveGestureActive) NoteRawWorkspaceRecipePreviewEdit(false);
        m_RawWorkspaceLabUi.calibrationEditor.dragging=-1;
        if(m_Project->nodeEditor) m_NodeEditorState.Get<Stack::Editor::RawLabInternal::CalibrationEditorState>(*m_Project->nodeEditor).dragging=-1;
        m_RawWorkspaceLabUi.zonesCurveGraph={};
        m_RawWorkspaceLabUi.toneCurveGraphs={};
        m_RawWorkspaceLabUi.sceneToneDraggingPoint=-1;
        m_RawWorkspaceLabUi.colorWarpInteractionActive=false;
        m_RawWorkspaceLabUi.colorWarpDraggingProvisional=false;
        m_RawWorkspaceLabUi.colorWarpProvisionalPinValid=false;
        m_RawWorkspaceLabUi.colorWarpProvisionalSceneEvValid=false;
        m_RawWorkspaceLabUi.gradientDragHandle=-1;
        m_RawWorkspaceLabUi.zoneAreas.active=false;
        for(auto& graph:m_RawWorkspaceLabUi.zoneAreas.graphs) graph.second.interaction={};
        m_EditingRawLayerMask.reset();m_RawLayerMaskGenerator=-1;m_RawLayerMaskDrag=-1;
        ClearRawWorkspaceGraphScopeReadbackCaches();
    }
    m_Project->nodeEditor=target;
    m_Project->layerNodeSelection[layerId]=uuid;
    m_SelectedRawAdjustmentLayer=layerId==kRawBackgroundId?std::string{}:layerId;
    if(m_RawLayerMaskWorkspace) m_RawLayerMaskWorkspace->layerId=layerId;
    const auto& editor=Stack::Editor::FindNodeEditor(*node);
    m_RawWorkspaceLabUi.activeTool=editor.legacyTool.value_or(RawLabTool::GraphNode);
    if(editor.toneView>=0) m_RawWorkspaceLabUi.sceneToneView=editor.toneView;
    if(node->kind==EditorNodeGraph::NodeKind::RawOperation)
        m_Project->rawOperationSelection[layerId+"/"+Stack::RawRecipe::GraphOperationId(node->rawOperation.kind)]=uuid;
    if(changed) MarkRenderRefreshDirty();
}

void EditorModule::BindRequestedRawNodeEditor() {
    m_Project->nodeEditor.reset();
    const auto* node=SelectedEditingNode();
    const auto* layer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    if(node && layer) SelectRawNodeEditor(layer->id,node->instanceUuid);
}

void EditorModule::ReconcileRawNodeEditorSelection() {
    // Source preparation and output presentation intentionally have no node
    // target. A graph edit must not replace those surfaces with an image tool.
    if (!m_Project->nodeEditor) return;
    if (m_Project->nodeEditor->layerId == kProjectGraphId) {
        if (!SelectedEditingNode()) m_Project->nodeEditor.reset();
        return;
    }
    const auto* current = SelectedEditingNode();
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_Project->nodeEditor->layerId);
    if (!layer) layer = &m_Project->rawLayers.State().background;
    const auto tools = Stack::Editor::RawLayerToolNodes(layer->graph);
    if (current && std::any_of(tools.begin(),tools.end(),[&](const auto* node) {
        return node->instanceUuid == current->instanceUuid;
    })) return;
    // A creation command can temporarily expose a shape's image handles.
    // Ordinary mask inspection still belongs to Graph, outside the picker.
    if (current && m_EditingRawLayerMask && m_EditingRawLayerMask->layerId == layer->id &&
        (current->kind == EditorNodeGraph::NodeKind::MaskTexture ||
         current->kind == EditorNodeGraph::NodeKind::MaskGenerator)) return;
    const auto remembered = m_Project->layerNodeSelection.find(layer->id);
    const auto previous = remembered == m_Project->layerNodeSelection.end() ? tools.end() :
        std::find_if(tools.begin(),tools.end(),[&](const auto* node) {
            return node->instanceUuid == remembered->second;
        });
    if (!tools.empty()) {
        const auto* selected = previous != tools.end() ? *previous : tools.front();
        // Do not use SelectRawAdjustmentLayer here. The caller may still be
        // publishing a graph draft, which that method would try to commit again.
        SelectRawNodeEditor(layer->id,selected->instanceUuid);
        return;
    }
    m_Project->rawLayers.EndGesture();
    if (m_RawWorkspaceAdaptiveGestureActive) NoteRawWorkspaceRecipePreviewEdit(false);
    m_SelectedRawAdjustmentLayer = layer->id == kRawBackgroundId ? std::string{} : layer->id;
    m_Project->nodeEditor.reset();
    m_Project->layerNodeSelection.erase(layer->id);
    m_RawWorkspaceLabUi.activeTool = RawLabTool::GraphNode;
    m_EditingRawLayerMask.reset();m_RawLayerMaskGenerator=-1;m_RawLayerMaskDrag=-1;
    ClearRawWorkspaceGraphScopeReadbackCaches();
}
