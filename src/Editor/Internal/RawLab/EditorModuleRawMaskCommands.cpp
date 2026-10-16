#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "App/WorkspacePresentation.h"
#include "Project/RawLayerStack.h"
#include "Project/RawNodeEditing.h"
#include "Editor/Internal/RawLab/RawLayerMaskPlacement.h"
#include "Editor/RawLayerTools.h"

#include <exception>

using namespace Stack::Project;

Stack::Editor::RawOperationMaskAvailability EditorModule::QueryRawOperationMaskAvailability() const {
    Stack::Editor::RawOperationMaskAvailability result;
    const auto disable = [&](const char* reason) { result.disabledReason = reason; return result; };
    if (Stack::Workspace::IsPreview()) return disable("Activate this project before creating a mask.");
    if (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->dirty)
        return disable("Finish the pending layer graph edit before creating a mask.");
    if (IsWorkspaceTransitionPending() || IsRawWorkspaceLockedByEditorProject())
        return disable("Wait for the RAW workspace to finish opening.");
    const auto phase = m_Project->lifecycle.Phase();
    if (phase == ProjectLifecyclePhase::Loading || phase == ProjectLifecyclePhase::Importing ||
        Async::IsBusy(m_Project->files->load.state))
        return disable("Wait for the project to finish loading.");
    if (phase == ProjectLifecyclePhase::Conflict)
        return disable("Resolve the project storage conflict before creating a mask.");
    if (phase == ProjectLifecyclePhase::ReadOnlyRecovery)
        return disable("Save a repaired copy before editing this recovered project.");
    const bool activeSource = !m_Project->rawSourceKey.empty() &&
        (IsBracketingToolActive() || IsRawWorkspaceProjectActive());
    const auto* source = FindRawWorkspaceSourceByKey(activeSource
        ? m_Project->rawSourceKey : m_RawWorkspace.selectedSourceKey);
    RawWorkspaceEditContext context;
    BeginRawWorkspaceEditContext(source, context);
    if (!context.canEdit) {
        result.disabledReason = context.error.empty() ? "Choose an editable RAW image first." : context.error;
        return result;
    }
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(), m_SelectedRawAdjustmentLayer);
    const auto* operation = SelectedEditingNode();
    if (!layer || !operation) return disable("Add or select an operation instance before creating a mask.");
    if (!RawNodeAcceptsMask(layer->graph,*operation))
        return disable("The selected operation has no coverage mask input.");
    const auto label = Stack::Editor::RawLayerToolLabel(layer->graph,*operation);
    result.target = Stack::Editor::RawOperationMaskTarget{m_Project->documentId,
        m_Project->files->load.generation, layer->id, operation->instanceUuid,
        label, layer->name};
    return result;
}

bool EditorModule::CreateRawOperationMask(const Stack::Editor::RawOperationMaskTarget& target,
    EditorNodeGraph::MaskGeneratorKind kind, std::string& error) {
    const auto available = QueryRawOperationMaskAvailability();
    if (!available.target) {
        error = available.disabledReason;
        return false;
    }
    if (!(target == *available.target)) {
        error = "The selected project, layer or operation changed. Open the mask menu again.";
        return false;
    }
    const auto* selected = SelectedEditingNode();
    if (!selected) { error = "Choose an operation for the local adjustment."; return false; }
    auto candidate = m_Project->rawLayers.State();
    const auto created = ApplyRawShapeMask(candidate,target.layerId,target.operationUuid,kind,error);
    if (created.layerId.empty()) return false;
    if (!ValidateRawLayerStack(candidate,error,&m_Project->graph,ResolveRawWorkspaceStageOutputNodeId())) return false;
    m_Project->rawLayers.EndGesture();
    if (!ApplyRawLayerStackEdit(std::move(candidate))) { error=m_RawLayerStatus; return false; }
    SelectRawAdjustmentLayer(created.layerId);
    SelectRawNodeEditor(created.layerId,created.operationUuid);
    m_EditingRawLayerMask = created.mask;
    m_RawLayerMaskGenerator = -1;
    m_RawLayerMaskDrag = Stack::Editor::RawLabInternal::MaskAwaitingPlacement;
    return true;
}
