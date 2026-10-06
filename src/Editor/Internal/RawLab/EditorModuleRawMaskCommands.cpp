#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "App/WorkspacePresentation.h"
#include "Project/RawLayerStack.h"

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
    if (!SelectedRawOperationKind())
        return disable("This control does not accept an operation mask.");
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
    const auto* operation = SelectedRawOperation();
    if (!layer || !operation) return disable("Add or select an operation instance before creating a mask.");
    const auto contract = EditorNodeGraphDefinitions::GetLivePortContract(layer->graph, *operation, "maskIn");
    if (!contract || contract->port.direction != Stack::NodeMath::PortDirection::Input ||
        contract->port.logicalType != Stack::NodeMath::LogicalValueType::Mask)
        return disable("The selected operation has no mask input.");
    int instance = 0, count = 0;
    for (const auto& node : layer->graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawOperation || node.rawOperation.kind != operation->rawOperation.kind)
            continue;
        ++count;
        if (node.instanceUuid == operation->instanceUuid) instance = count;
    }
    const auto label = count > 1 ? operation->title + " " + std::to_string(instance) : operation->title;
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
    return CreateRawLayerMask(target.layerId, target.operationUuid, kind, error);
}

bool EditorModule::CreateRawLayerMask(const std::string& layerId, const std::string& operationUuid,
    EditorNodeGraph::MaskGeneratorKind kind, std::string& error) {
    error.clear();
    const char* name = nullptr;
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: name = "Radial"; break;
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: name = "Linear"; break;
        case EditorNodeGraph::MaskGeneratorKind::Square: name = "Custom mask"; break;
        default: error = "This mask type is not available in the RAW mask menu."; return false;
    }
    try {
        auto candidate = m_Project->rawLayers.State();
        if (!FindRawAdjustmentLayer(candidate, layerId)) {
            error = "The mask's layer no longer exists.";
            return false;
        }
        const auto mask = AddRawGeneratedMask(candidate, layerId, kind, name);
        if (operationUuid.empty()) FindRawAdjustmentLayer(candidate, layerId)->layerMask = mask;
        else if (!SetRawOperationMask(candidate, layerId, operationUuid, mask, error)) return false;
        // Validate before ending a pending control gesture. A rejected candidate
        // must not change even the history grouping of the current document.
        if (!ValidateRawLayerStack(candidate, error, &m_Project->graph, ResolveRawWorkspaceStageOutputNodeId()))
            return false;
        m_Project->rawLayers.EndGesture();
        if (!ApplyRawLayerStackEdit(std::move(candidate))) {
            error = m_RawLayerStatus;
            return false;
        }
        m_EditingRawLayerMask = mask;
        m_RawLayerMaskGenerator = -1;
        m_RawLayerMaskDrag = -1;
        m_RawLayerStatus.clear();
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}
