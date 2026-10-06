#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabAreaImage.h"

void EditorModule::RenderRawWorkspaceLabImageAreas(const Stack::RawWorkspace::SourceRecord* source,
    const ImVec2& minimum, const ImVec2& maximum) {
    if (m_EditingRawLayerMask) return;
    auto& ui = m_RawWorkspaceLabUi.zoneAreas;
    if (m_RawWorkspaceLabUi.activeTool != RawLabTool::Zones || !m_RawWorkspaceLabUi.zonesTargetedView ||
        m_RawWorkspaceLocalRangeTargetMode) return;
    RawWorkspaceEditContext context;
    if (!BeginRawWorkspaceEditContext(source, context) || !context.canEdit) return;
    const std::string identity = GetActiveRawWorkspacePreviewIdentity();
    if (ui.sourceKey != identity) return;
    Async::ActivityMetadata activity;
    activity.ownerId = GetNotifier().GetOwner().id;
    activity.ownerGeneration = GetNotifier().GetOwner().generation;
    activity.maintenance = true;
    std::optional<Stack::Editor::RawLabInternal::RawLabAreaImageMappings> mappings;
    auto editingRecipe = context.recipe;
    if (!context.rawAdjustmentLayerId.empty()) {
        if (m_GraphOutputDescriptionRenderRevision != m_RenderRevision) BuildGraphSnapshot();
        const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),context.rawAdjustmentLayerId);
        const auto* operation = SelectedRawOperation();
        if (!layer || !operation) return;
        using Mapping = Stack::Editor::RawLabInternal::RawLabImageMapping;
        const auto mappingFor = [&](int nodeId,const std::string& port) -> std::optional<Mapping> {
            const auto found=m_GraphOutputDescriptions.find("raw/"+layer->id+"/"+EditorNodeGraph::GraphOutputIdentity(nodeId,port));
            if (found==m_GraphOutputDescriptions.end() || found->second.descriptor.spatial.state!=Stack::NodeMath::KnowledgeState::Known ||
                m_LastGraphOutputSemanticDescriptor.spatial.state!=Stack::NodeMath::KnowledgeState::Known) return std::nullopt;
            return Mapping::FromSpatial(found->second.descriptor.spatial.value,
                m_LastGraphOutputSemanticDescriptor.spatial.value,context.recipe.cropRotation);
        };
        const auto* input=layer->graph.FindInputLink(operation->id,"referenceIn");
        if (!input) input=layer->graph.FindInputLink(operation->id,"imageIn");
        const auto measured=input ? mappingFor(input->fromNodeId,input->fromSocketId) : std::nullopt;
        if (!measured) {
            ImGui::GetWindowDrawList()->AddText({minimum.x+12,minimum.y+12},IM_COL32(230,230,230,255),
                "The Local EV measurement image coordinates are unavailable.");
            return;
        }
        std::unordered_map<std::string,Mapping> areaMappings;
        for (const auto& area : editingRecipe.localRange.areas) {
            const auto* owner=Stack::Project::FindRawCoverageOwner(*layer,operation->id,"area:"+area.id,area.id);
            if (owner) if (const auto mapping=mappingFor(owner->id,"maskOut")) areaMappings.emplace(area.id,*mapping);
        }
        mappings.emplace();
        mappings->measurement=*measured;
        mappings->area=[values=std::move(areaMappings)](const std::string& id) -> std::optional<Mapping> {
            const auto found=values.find(id);
            return found==values.end() ? std::nullopt : std::optional<Mapping>(found->second);
        };
        // Graph generators already operate in the selected input's coordinates.
        // Project crop and orientation belong only to the viewport mapping.
        editingRecipe.cropRotation={};
    }
    const auto edit = Stack::Editor::RawLabInternal::InteractRawLabAreaImage(
        ui, editingRecipe, m_RawWorkspaceGraphScopeReadback, minimum, maximum, activity,mappings ? &*mappings : nullptr);
    context.recipe.localRange=editingRecipe.localRange;
    if (edit.changed || edit.finished) CommitRawWorkspaceEditContext(context, edit.changed, edit.active);
    if (edit.maskEdited) {
        m_RawWorkspaceAnalysisPending = true;
        m_RawWorkspaceAnalysisRequested = false;
        m_RawWorkspaceAnalysisQuietUntilTime = ImGui::GetTime() + .25;
    }
    Stack::Editor::RawLabInternal::DrawRawLabAreaImage(
        ui, editingRecipe, m_RawWorkspaceGraphScopeReadback, minimum, maximum, activity,mappings ? &*mappings : nullptr);
}
