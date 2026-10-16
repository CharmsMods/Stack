#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Editor/LayerRegistry.h"
#include "Project/RawLayerStack.h"

bool EditorModule::ApplyProjectGraphDocumentEdit(const std::string& document,
    std::uint64_t revision,Stack::GraphModel::EditProposal proposal,
    Stack::Timeline::TimelineAnimationState animation,bool gesture,std::string& error,bool completesAction) {
    if (document!=GetProjectDocumentId() || revision!=m_Project->editRevision) {
        error="The project changed while this edit was prepared.";return false;
    }
    // The proposal already owns the candidate. Do not copy source image bytes
    // into a temporary graph which ApplyEdit immediately replaces.
    EditorNodeGraph::Graph candidate;
    if (!Stack::GraphModel::ApplyEdit(candidate,m_Project->graph.GetStructureRevision(),std::move(proposal),error)) return false;
    Stack::GraphModel::ReindexLayerParameterRecords(candidate);
    if ((IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) &&
        !Stack::Project::ValidateRawLayerStack(m_Project->rawLayers.State(),error,&candidate,ResolveRawWorkspaceStageOutputNodeId())) return false;
    const auto settings=Stack::GraphModel::LayerParameterRecords(candidate);
    std::vector<std::shared_ptr<LayerBase>> adapters;
    try {
        for (std::size_t i=0;i<settings.size();++i) {
            if (i<m_Project->layers.size() && m_Project->layers[i] &&
                Stack::GraphModel::CaptureLayerParameters(*m_Project->layers[i])==settings[i]) {
                adapters.push_back(m_Project->layers[i]);continue;
            }
            auto adapter=LayerRegistry::CreateLayerFromTypeId(settings[i].at("type").get<std::string>());
            if (!adapter) {error="An operation in this edit is unavailable.";return false;}
            adapter->InitializeGL();Stack::GraphModel::PrepareLayerParameterDraft(*adapter,settings[i]);
            adapters.push_back(std::move(adapter));
        }
    } catch (const std::exception& exception) {error=exception.what();return false;}
    if (!m_Project->restoringGraphHistory && !m_Project->graphHistory.HasBaseline())
        m_Project->graphHistory.Begin(CaptureProjectGraphState());
    Stack::GraphModel::RetainImageStorage(candidate,m_Project->graph);
    m_Project->graph=std::move(candidate);m_Project->layers=std::move(adapters);
    m_Project->timeline=std::move(animation);
    if (!m_Project->restoringGraphHistory) {
        if (completesAction) m_Project->graphHistory.CompleteAsyncEdit(CaptureProjectGraphState());
        else m_Project->graphHistory.Observe(CaptureProjectGraphState(),gesture);
    }
    MarkRenderDirty();return true;
}
