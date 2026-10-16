#include "Editor/EditorModule.h"

#include "App/WorkspacePresentation.h"
#include "Persistence/ProjectSnapshotCapture.h"

#include <exception>
#include <utility>

bool EditorModule::CanAddProjectOutputToQueue(int outputNodeId, std::string* reason) const {
    const auto unavailable = [&](const char* message) {
        if (reason) *reason = message;
        return false;
    };
    if (reason) reason->clear();
    if (!m_ProjectQueueRequestHandler || Stack::Workspace::IsPreview())
        return unavailable("The Queue is unavailable in this workspace.");
    if (IsEditingRawLayerMaskGraph() && !IsRawProjectCanvas())
        return unavailable("Add the final Image Output from the project Graph.");
    if (IsRawProjectCanvas() && outputNodeId != m_RawLayerMaskWorkspace->projectCanvas->output)
        return unavailable("Add the final Image output at the end of the project Graph to Queue.");
    if (!HasOpenProjectSession())
        return unavailable("Open a project before adding an Output to Queue.");
    if (IsRawWorkspaceProjectLoadBusy() || IsSourceLoadBusy() ||
        Async::IsBusy(GetProjectLoadTaskState()) || IsGraphDropImportBusy())
        return unavailable("Wait for the project and its imported images to finish opening.");
    if (HasPendingBracketingDraft())
        return unavailable("Process or discard pending Bracketing input changes before adding the result to Queue.");
    if (IsRawWorkspaceProjectActive() && !IsUnifiedProjectStoreActive())
        return unavailable("Finish creating the RAW project before adding its current edits to Queue.");
    return true;
}

bool EditorModule::AddProjectOutputToQueue(int outputNodeId) {
    const auto fail = [&](const std::string& message) {
        PostNotification(UiNotificationSeverity::Warning, message,
            "project-add-to-queue");
        return false;
    };
    std::string reason;
    if (!CanAddProjectOutputToQueue(outputNodeId, &reason)) return fail(reason);
    const auto& visibleGraph = GetNodeGraph();
    const auto* requestedOutput = visibleGraph.FindNode(outputNodeId);
    if (!requestedOutput || requestedOutput->kind != EditorNodeGraph::NodeKind::Output ||
        !visibleGraph.FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId))
        return fail("Connect an image to this Output before adding it to Queue.");
    const bool rawCanvasOutput = IsRawProjectCanvas();
    if (rawCanvasOutput && outputNodeId != m_RawLayerMaskWorkspace->projectCanvas->output)
        return fail("Add the final Image output at the end of the project Graph to Queue.");
    const auto outputUuid = requestedOutput->instanceUuid;
    if (HasPendingGraphImageImports())
        return fail("Wait for the project's imported images to finish opening.");
    if (visibleGraph.IsOutputChannelInspection(outputNodeId))
        return fail("Queue export requires an Image. Connect this Channel to Image Combine first.");
    if (!FinishWorkspaceInteraction())
        return fail("Finish the current edit before adding this Output to Queue.");

    try {
        // Serialize only for this explicit action. Retain the pipeline's CPU
        // source without a viewport readback; authored Image nodes use their
        // normal storage representation, encoding edited pixels when needed.
        auto candidate = std::make_shared<LoadedProjectData>();
        candidate->pipelineData = SerializePipeline();
        Stack::Queue::ProjectRequest request;
        request.outputNodeUuid = outputUuid;
        request.editRevision = m_Project->editRevision;
        request.displayName = m_Project->name.empty() ? "Untitled Project" : m_Project->name;

        // Queue this terminal alone, even when the open Graph has several
        // enabled Outputs. The live Graph's activation and selection stay owned
        // by the editor; the captured document selects its own single result.
        auto& graph = candidate->pipelineData.at("nodeGraph");
        if (rawCanvasOutput) {
            // The RAW canvas's Image output is a projection of the owned layer
            // routing, not an integer node in the outer source graph. Lowering
            // replaces the prepared-source endpoint with that routed result.
            // Bind a captured outer Output directly to it, keeping all live
            // topology and the saved project untouched.
            const int sourceId = ResolveRawWorkspaceStageOutputNodeId();
            outputNodeId = m_Project->graph.GetOutputNodeId();
            const auto* outerOutput = m_Project->graph.FindNode(outputNodeId);
            if (!outerOutput || outerOutput->kind != EditorNodeGraph::NodeKind::Output || sourceId <= 0)
                return fail("The RAW project's final image terminal is unavailable.");
            auto& links = graph.at("links");
            for (auto iterator = links.begin(); iterator != links.end();) {
                if (iterator->value("toNodeId", -1) == outputNodeId &&
                    iterator->value("toSocket", std::string()) == EditorNodeGraph::kImageInputSocketId)
                    iterator = links.erase(iterator);
                else ++iterator;
            }
            links.push_back({{"fromNodeId", sourceId}, {"fromSocket", EditorNodeGraph::kImageOutputSocketId},
                {"toNodeId", outputNodeId}, {"toSocket", EditorNodeGraph::kImageInputSocketId},
                {"ownership", "user"}});
        } else if (!m_Project->graph.FindNode(outputNodeId)) {
            return fail("The selected Output changed. Add it to Queue again.");
        }
        graph["outputNodeIds"] = nlohmann::json::array({outputNodeId});
        graph["selectedNodeId"] = outputNodeId;
        for (auto& node : graph.at("nodes")) {
            if (node.value("kind", std::string()) == "Output")
                node["outputEnabled"] = node.value("id", -1) == outputNodeId;
        }
        candidate->projectName = request.displayName;
        candidate->projectFileName = m_Project->fileName;
        candidate->projectId = EnsureProjectDocumentId();
        candidate->adoptedFrom = m_Project->adoptionSourcePath;

        const bool rawProject = IsRawWorkspaceProjectActive();
        candidate->projectKind = rawProject ? StackBinaryFormat::kRawProjectKind :
            StackBinaryFormat::kEditorProjectKind;
        if (IsUnifiedProjectStoreActive()) {
            candidate->rawProjectSnapshot = std::make_shared<Stack::Project::RawProjectSnapshot>(
                Stack::Project::CopyProjectSnapshotWithoutPipeline(*m_Project->snapshot));
            candidate->projectStore = m_Project->store;
            candidate->decodedRawSource = m_Project->singleRawSource;
            candidate->rawWorkspaceData = m_Project->snapshot->rawWorkspaceData;
            request.thumbnailBytes = m_Project->snapshot->coverThumbnailBytes;
            if (rawProject && !IsMultiFrameRawProjectActive()) {
                candidate->rawWorkspaceData["rawRecipe"] =
                    Stack::RawRecipe::SerializeWorkspaceSourceRecipe(m_Project->rawRecipe);
                StackBinaryFormat::ProjectDocument modeDocument;
                modeDocument.rawWorkspaceData = std::move(candidate->rawWorkspaceData);
                ApplyActiveRawWorkspaceModeDataToDocument(modeDocument);
                candidate->rawWorkspaceData = std::move(modeDocument.rawWorkspaceData);
            }
            candidate->rawProjectSnapshot->projectName = candidate->projectName;
            candidate->rawProjectSnapshot->pipelineData = candidate->pipelineData;
            candidate->rawProjectSnapshot->rawWorkspaceData = candidate->rawWorkspaceData;
        }
        if (rawProject) {
            candidate->sourceState = ProjectSourceState::LazyAsset;
        } else {
            candidate->sourcePixelsShared = m_Pipeline.ShareSourcePixels(
                candidate->width, candidate->height, candidate->channels);
            if (candidate->sourcePixelsShared.empty()) {
                // Graph-owned images/assets can provide every source. A neutral
                // legacy pipeline input keeps such Graph documents loadable.
                candidate->sourcePixels.assign(4, 0);
                candidate->width = candidate->height = 1;
                candidate->channels = 4;
            }
        }
        request.snapshot = std::move(candidate);
        const auto result = m_ProjectQueueRequestHandler(request);
        Stack::Notifications::NoticeSpec notice;
        notice.message = result.added ? "Added current edits to Queue." : "The Output could not be added to Queue.";
        notice.context = request.displayName;
        notice.dedupeKey = "project-add-to-queue";
        notice.severity = result.added ? UiNotificationSeverity::Success : UiNotificationSeverity::Warning;
        notice.outcome = result.added ? Stack::Notifications::Outcome::Success : Stack::Notifications::Outcome::Failure;
        for (const auto& error : result.errors) notice.details += error + "\n";
        GetNotifier().Post(std::move(notice));
        return result.added != 0;
    } catch (const std::exception& error) {
        return fail(std::string("The current Output could not be captured for Queue: ") + error.what());
    }
}
