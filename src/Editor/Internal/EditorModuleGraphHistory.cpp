#include "Editor/EditorModule.h"
#include "Graph/LayerOperationParameters.h"
#include "Graph/GraphDocumentRules.h"
#include "Project/GraphImagePayload.h"
#include "NodeMath/ToneCurveParameters.h"

Stack::Project::ProjectGraphState EditorModule::CaptureProjectGraphState() const {
    Stack::Project::ProjectGraphState state;
    state.graph=m_Project->graph.CloneForAnalysis();
    state.animation=m_Project->timeline;
    for (const auto& node:m_Project->graph.GetNodes()) if (node.kind==EditorNodeGraph::NodeKind::Image) {
        auto& storage=state.images[node.instanceUuid];
        storage.pixels=Stack::Project::EnsureSharedImagePixels(node.image);
        storage.png=m_Project->graphHistory.RetainImageBytes(node.instanceUuid+"/png",node.image.pngBytes);
        storage.preview=m_Project->graphHistory.RetainImageBytes(node.instanceUuid+"/preview",node.image.previewPixels);
        storage.previewWidth=node.image.previewWidth; storage.previewHeight=node.image.previewHeight;
        storage.previewChannels=node.image.previewChannels;
        auto* retained=state.graph.FindNode(node.id);
        retained->image.sharedPixels=storage.pixels.bytes;
        retained->image.pixelsFingerprint=storage.pixels.fingerprint;
        retained->rawLayerOwnerId=node.rawLayerOwnerId;
        retained->exposedParameterIds=node.exposedParameterIds;
    }
    return state;
}
void EditorModule::BeginProjectGraphHistory() {
    if (!IsEditingRawLayerMaskGraph() && !m_Project->restoringGraphHistory && !m_Project->graphHistory.HasBaseline())
        m_Project->graphHistory.Begin(CaptureProjectGraphState());
}
void EditorModule::ObserveProjectGraphEdit(bool gesture) {
    if (!IsEditingRawLayerMaskGraph() && !m_Project->restoringGraphHistory)
        m_Project->graphHistory.Observe(CaptureProjectGraphState(), gesture);
}
bool EditorModule::RestoreProjectGraphHistory(bool redo, bool cancel) {
    if (IsEditingRawLayerMaskGraph()) return redo ? RedoRawLayerEdit() : UndoRawLayerEdit();
    auto& history = m_Project->graphHistory;
    const auto* candidate = cancel ? history.CancelCandidate() : history.Candidate(redo);
    if (!candidate) return false;
    const bool affectsPixels = cancel ? history.CancelAffectsPixels() : history.CandidateAffectsPixels(redo);
    if (!affectsPixels) {
        // Notes-only history must not replace source results accepted after the
        // note was written. Restore annotations by stable instance identity.
        for (auto& node:m_Project->graph.GetNodes()) {
            const auto* saved=candidate->graph.FindNode(node.id);
            if (saved && saved->instanceUuid==node.instanceUuid) node.notes=saved->notes;
        }
        if (cancel) history.AcceptCancel(); else history.AcceptRestore(redo);
        history.RefreshCurrent(CaptureProjectGraphState());
        MarkDirty(false);
        return true;
    }
    auto state = *candidate;
    for (auto& node:state.graph.GetNodes()) if (node.kind==EditorNodeGraph::NodeKind::RawOperation &&
        node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::ToneCurve)
        Stack::NodeMath::ClearPendingToneCurveAnalysis(node.rawOperation.parameters);
    // The reduced graph is never committed by itself. Restore its source
    // payloads first; all history entries share these immutable byte owners.
    for (auto& node:state.graph.GetNodes()) if (node.kind==EditorNodeGraph::NodeKind::Image) {
        const auto found=state.images.find(node.instanceUuid);
        if (found==state.images.end()) return false;
        const auto& storage=found->second;
        if (!storage.pixels.empty()) node.image.pixels=*storage.pixels.bytes;
        if (storage.png) node.image.pngBytes=*storage.png;
        if (storage.preview) node.image.previewPixels=*storage.preview;
        node.image.previewWidth=storage.previewWidth; node.image.previewHeight=storage.previewHeight;
        node.image.previewChannels=storage.previewChannels;
    }
    auto context = GetGraphEditorContext();
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph,context.revision,
        [&](auto& graph) { graph = state.graph; });
    struct RestoreScope {
        bool& flag;
        RestoreScope(bool& value) : flag(value) { flag = true; }
        ~RestoreScope() { flag = false; }
    } scope(m_Project->restoringGraphHistory);
    std::string error;
    if (!context.applyDocumentEdit(std::move(proposal),Stack::GraphModel::LayerParameterRecords(state.graph),state.animation,error)) {
        ShowUiNotification(UiNotificationSeverity::Error,error,"graph-history"); return false;
    }
    if (cancel) history.AcceptCancel(); else history.AcceptRestore(redo);
    if (m_Project->nodeEditor && !SelectedEditingNode()) m_Project->nodeEditor.reset();
    return true;
}
