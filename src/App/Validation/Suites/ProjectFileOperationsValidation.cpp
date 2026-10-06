#include "App/Validation/Suites/ProjectFileOperationsValidation.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Persistence/ProjectOpenCoordinator.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Completion {
    unsigned calls = 0;
    bool success = false;
    void Finish(bool result) { ++calls; success = result; }
};

void WaitForBoth(EditorModule& a, EditorModule& b,
    const std::shared_ptr<Completion>& first, const std::shared_ptr<Completion>& second) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while ((!first->calls || !second->calls) && std::chrono::steady_clock::now() < deadline) {
        ImGui::NewFrame();
        a.PumpNonRenderingWork(2.5, false);
        b.PumpNonRenderingWork(2.5, false);
        ImGui::EndFrame();
        Async::TaskSystem::Get().PumpMainThreadTasks(4);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(first->calls == 1 && second->calls == 1 && first->success && second->success,
        "Overlapping project operations did not both complete successfully within 15 seconds.");
}

nlohmann::json Graph(const EditorModule& editor) {
    return EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), editor.GetNodeGraph());
}
}

void ValidateOverlappingProjectFileOperations(
    EditorModule& a, EditorModule& b, const std::filesystem::path& scratch) {
    // The preceding fixture may already have closed either document.
    // CloseCurrentProject returns false when there is no document to close.
    const auto prepare = [](EditorModule& editor) {
        return editor.GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty ||
            editor.CloseCurrentProject(true);
    };
    Require(prepare(a) && prepare(b),
        "Could not prepare the overlapping project operation check.");
    const auto pathA = scratch / "Overlapping save A";
    const auto pathB = scratch / "Overlapping save B";
    a.SetDocumentPersistenceEnabled(true);
    b.SetDocumentPersistenceEnabled(true);
    a.SetCurrentProjectName("Overlapping save A");
    b.SetCurrentProjectName("Overlapping save B");
    a.SetCurrentProjectFileName(pathA.string());
    b.SetCurrentProjectFileName(pathB.string());
    a.AddReformatNodeAt({20, 40});
    b.AddReformatNodeAt({80, 120});
    // An unconnected Output preserves the authored graph without requiring
    // image rendering before the ordinary load callbacks can complete.
    a.AddOutputNodeAt({360, 40});
    b.AddOutputNodeAt({420, 120});
    const auto graphA = Graph(a);
    const auto graphB = Graph(b);
    const auto pipelineA = a.SerializePipeline();
    const auto pipelineB = b.SerializePipeline();
    const std::string idA = a.EnsureProjectDocumentId();
    const std::string idB = b.EnsureProjectDocumentId();
    Require(idA != idB && graphA != graphB, "The overlap check did not create distinct projects.");

    auto savedA = std::make_shared<Completion>();
    auto savedB = std::make_shared<Completion>();
    Require(a.RequestSaveCurrentProject({}, [savedA](bool success) { savedA->Finish(success); }),
        "The first overlapping save was rejected.");
    Require(b.RequestSaveCurrentProject({}, [savedB](bool success) { savedB->Finish(success); }),
        "The second overlapping save was rejected.");
    Require(a.IsProjectFileSaveBusy() && b.IsProjectFileSaveBusy(),
        "Both project saves were not independently in flight.");
    a.AddReformatNodeAt({180, 220});
    const auto editedGraphA = Graph(a);
    // These calls deliberately exercise explicit saves. Disable periodic
    // autosave so the accelerated ImGui clock cannot save the later edit.
    a.SetDocumentPersistenceEnabled(false);
    b.SetDocumentPersistenceEnabled(false);
    WaitForBoth(a, b, savedA, savedB);
    Require(a.IsDirty() && !b.IsDirty() && Graph(a) == editedGraphA && Graph(b) == graphB,
        "A save completion cleared a later edit or changed the other project.");
    Require(a.IsUnifiedProjectStoreActive() && b.IsUnifiedProjectStoreActive() &&
        !a.IsRawWorkspaceProjectActive() && !b.IsRawWorkspaceProjectActive() &&
        a.GetProjectLifecyclePhase() == Project::ProjectLifecyclePhase::ReadyDirty &&
        b.GetProjectLifecyclePhase() == Project::ProjectLifecyclePhase::ReadyClean,
        "First saves did not attach independent ordinary-project baselines with the correct dirty state.");
    const auto openedA = Project::ProjectOpenCoordinator::Load(pathA);
    const auto openedB = Project::ProjectOpenCoordinator::Load(pathB);
    Require(openedA && openedB, "The overlapping saves did not produce readable projects.");
    Require(openedA.candidate->projectId == idA && openedB.candidate->projectId == idB &&
        openedA.candidate->projectName == "Overlapping save A" &&
        openedB.candidate->projectName == "Overlapping save B" &&
        openedA.candidate->pipelineData == pipelineA && openedB.candidate->pipelineData == pipelineB,
        "The saved project manifests did not match their captured owners and graphs.");
    std::cout << "PASS overlapping ordinary saves retain their captured graphs and preserve later edits\n";

    Require(a.CloseCurrentProject(true) && b.CloseCurrentProject(false),
        "Could not close the overlapping saved projects.");
    auto loadedA = std::make_shared<Completion>();
    auto loadedB = std::make_shared<Completion>();
    LibraryManager::Get().RequestLoadProjectFromPath(pathA, &a,
        [loadedA](bool success) { loadedA->Finish(success); });
    LibraryManager::Get().RequestLoadProjectFromPath(pathB, &b,
        [loadedB](bool success) { loadedB->Finish(success); });
    Require(Async::IsBusy(a.GetProjectLoadTaskState()) &&
        Async::IsBusy(b.GetProjectLoadTaskState()),
        "Both project loads were not independently in flight.");
    WaitForBoth(a, b, loadedA, loadedB);
    Require(a.GetProjectDocumentId() == idA && b.GetProjectDocumentId() == idB &&
        a.GetCurrentProjectName() == "Overlapping save A" &&
        b.GetCurrentProjectName() == "Overlapping save B" &&
        std::filesystem::path(a.GetCurrentProjectFileName()).lexically_normal() == pathA.lexically_normal() &&
        std::filesystem::path(b.GetCurrentProjectFileName()).lexically_normal() == pathB.lexically_normal() &&
        Graph(a) == graphA && Graph(b) == graphB && !a.IsDirty() && !b.IsDirty(),
        "An overlapping project load applied its data to the wrong owner.");
    std::cout << "PASS overlapping ordinary loads complete for their own project instances\n";

    const auto revisionBeforeRename = a.GetProjectEditRevision();
    auto renamedA = std::make_shared<Completion>();
    a.SetDocumentPersistenceEnabled(true);
    Require(a.RequestSaveCurrentProject("  Renamed overlap A  ",
        [renamedA](bool success) { renamedA->Finish(success); }),
        "The managed project rename save was rejected.");
    a.SetDocumentPersistenceEnabled(false);
    WaitForBoth(a, b, renamedA, loadedB);
    const auto renamed = Project::ProjectOpenCoordinator::Load(pathA);
    const auto unchangedB = Project::ProjectOpenCoordinator::Load(pathB);
    Require(renamed && unchangedB && renamed.candidate->rawProjectSnapshot &&
        unchangedB.candidate->rawProjectSnapshot && openedB.candidate->rawProjectSnapshot &&
        a.GetCurrentProjectName() == "Renamed overlap A" &&
        a.GetProjectEditRevision() > revisionBeforeRename && !a.IsDirty() &&
        renamed.candidate->projectName == "Renamed overlap A" &&
        renamed.candidate->projectId == idA && Graph(a) == graphA &&
        unchangedB.candidate->pipelineData == pipelineB &&
        unchangedB.candidate->rawProjectSnapshot->persistedStorageRevision ==
            openedB.candidate->rawProjectSnapshot->persistedStorageRevision &&
        b.GetProjectDocumentId() == idB && b.GetCurrentProjectName() == "Overlapping save B" &&
        Graph(b) == graphB && !b.IsDirty(),
        "A managed rename was lost or changed the other project's data or saved revision.");
    std::cout << "PASS managed project rename persists without changing the other project\n";

    a.AddLayerNodeAt(LayerType::Brightness, {220, 260});
    for (const auto& node : a.GetNodeGraph().GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Layer ||
            node.layerType != LayerType::Brightness) continue;
        const auto parameters = Timeline::CollectAnimatableParametersForNode(node);
        Require(!parameters.empty(), "The Save As check could not find an animatable parameter.");
        Timeline::SetOrReplaceKeyframe(a.GetProjectSession().timeline,
            parameters.front().target, 12, 0.35f);
        break;
    }
    a.MarkDirty();
    const auto timelineForCopy = a.SerializePipeline().at("editorTimeline");
    Require(timelineForCopy != renamed.candidate->pipelineData.at("editorTimeline"),
        "The Save As check did not create a new timeline edit.");
    const auto copyPath = scratch / "Timeline save copy";
    auto copiedA = std::make_shared<Completion>();
    Require(a.RequestSaveProjectAs(copyPath,
        [copiedA](bool success) { copiedA->Finish(success); }),
        "Save As rejected the current timeline edit.");
    WaitForBoth(a, b, copiedA, loadedB);
    const auto copied = Project::ProjectOpenCoordinator::Load(copyPath);
    const auto originalAfterCopy = Project::ProjectOpenCoordinator::Load(pathA);
    Require(copied && originalAfterCopy && originalAfterCopy.candidate->rawProjectSnapshot &&
        copied.candidate->projectId != idA &&
        a.GetProjectDocumentId() == copied.candidate->projectId &&
        copied.candidate->pipelineData.at("editorTimeline") == timelineForCopy &&
        originalAfterCopy.candidate->pipelineData == renamed.candidate->pipelineData &&
        originalAfterCopy.candidate->rawProjectSnapshot->persistedStorageRevision ==
            renamed.candidate->rawProjectSnapshot->persistedStorageRevision &&
        Graph(b) == graphB && !b.IsDirty(),
        "Save As lost the current timeline edit or changed an existing project.");
    std::cout << "PASS Save As retains current timeline edits and preserves the original project\n";

    auto canceledLoad = std::make_shared<Completion>();
    Require(a.BeginDeferredLoadedProjectApply(openedA.candidate,
        [canceledLoad](bool success, const std::string&) { canceledLoad->Finish(success); }) &&
        a.IsDeferredLoadedProjectApplyActive(),
        "Could not begin the deferred project load cancellation check.");
    auto rejectedApply = std::make_shared<Completion>();
    auto rejectedLoad = std::make_shared<Completion>();
    Require(!a.BeginDeferredLoadedProjectApply(openedB.candidate,
        [rejectedApply](bool success, const std::string&) { rejectedApply->Finish(success); }),
        "A second deferred apply displaced the pending project load.");
    LibraryManager::Get().RequestLoadProjectFromPath(pathB, &a,
        [rejectedLoad](bool success) { rejectedLoad->Finish(success); });
    Require(rejectedApply->calls == 0 && rejectedLoad->calls == 1 && !rejectedLoad->success &&
        canceledLoad->calls == 0 && a.IsDeferredLoadedProjectApplyActive(),
        "Rejecting an overlapping load changed the original apply or its completion.");
    Require(a.CloseCurrentProject(true) && canceledLoad->calls == 1 && !canceledLoad->success &&
        !a.IsDeferredLoadedProjectApplyActive(),
        "Closing a project did not cancel its pending deferred load exactly once.");
    const auto closedGraph = Graph(a);
    for (unsigned frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        a.PumpNonRenderingWork(2.5, false);
        b.PumpNonRenderingWork(2.5, false);
        ImGui::EndFrame();
        Async::TaskSystem::Get().PumpMainThreadTasks(4);
    }
    Require(a.GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty &&
        a.GetCurrentProjectFileName().empty() && a.GetProjectDocumentId().empty() &&
        Graph(a) == closedGraph && canceledLoad->calls == 1 &&
        b.GetProjectDocumentId() == idB && Graph(b) == graphB && !b.IsDirty(),
        "A canceled deferred load reinstalled its document or changed another project after closing.");
    std::cout << "PASS closing before deferred apply cancels completion and prevents later installation\n";

    const auto renamePath = scratch / "Queued rename A";
    a.SetCurrentProjectName("Initial name A");
    a.SetCurrentProjectFileName(renamePath.string());
    a.AddReformatNodeAt({30, 50});
    a.AddOutputNodeAt({370, 50});
    const auto renameGraph = Graph(a);
    const auto renameId = a.EnsureProjectDocumentId();
    auto firstNameSave = std::make_shared<Completion>();
    auto queuedNameSave = std::make_shared<Completion>();
    a.SetDocumentPersistenceEnabled(true);
    Require(a.RequestSaveCurrentProject({},
        [firstNameSave](bool success) { firstNameSave->Finish(success); }) &&
        a.RequestSaveCurrentProject("Queued rename A",
            [queuedNameSave](bool success) { queuedNameSave->Finish(success); }),
        "Could not queue a rename while the first project save was pending.");
    a.SetDocumentPersistenceEnabled(false);
    WaitForBoth(a, b, firstNameSave, queuedNameSave);
    const auto queuedRename = Project::ProjectOpenCoordinator::Load(renamePath);
    Require(queuedRename && queuedRename.candidate->projectId == renameId &&
        queuedRename.candidate->projectName == "Queued rename A" &&
        a.GetCurrentProjectName() == "Queued rename A" && Graph(a) == renameGraph && !a.IsDirty() &&
        b.GetProjectDocumentId() == idB && Graph(b) == graphB && !b.IsDirty(),
        "The first save completion overwrote a queued rename or changed another project.");
    Require(a.CloseCurrentProject(false) && b.CloseCurrentProject(false),
        "Could not close the independently saved projects.");
    std::cout << "PASS a rename queued during the first save persists without being overwritten\n";
    const auto folder = scratch / "first-save-folder";
    std::filesystem::create_directories(folder);
    a.SetRawWorkspaceFolder(folder);
    a.SetCurrentProjectName("Graph in loaded folder");
    a.AddReformatNodeAt({30, 50});
    a.AddOutputNodeAt({370, 50});
    const auto folderGraph = Graph(a);
    auto folderSave = std::make_shared<Completion>();
    a.SetDocumentPersistenceEnabled(true);
    Require(a.RequestSaveCurrentProject({},
        [folderSave](bool success) { folderSave->Finish(success); }), "The folder's first Graph save was rejected.");
    a.SetDocumentPersistenceEnabled(false);
    WaitForBoth(a, b, folderSave, queuedNameSave);
    const auto layout = RawWorkspace::BuildManagedLayout(folder);
    const auto savedPath = std::filesystem::path(a.GetCurrentProjectFileName());
    Require(savedPath.parent_path() == layout.projectsDirectory && Graph(a) == folderGraph && !a.IsDirty(),
        "A first Graph save escaped the loaded RAW folder or changed authored state.");
    std::vector<RawWorkspace::SourceRecord> folderSources;
    std::vector<RawWorkspace::SourceSetProjectCatalogEntry> folderProjects;
    Require(RawWorkspace::DiscoverSourceSetProjects(layout, folderSources, folderProjects) &&
        folderProjects.size() == 1 && folderProjects.front().projectId == a.GetProjectDocumentId(),
        "The saved Graph document was not discoverable in its folder's Projects section.");
    Require(a.CloseCurrentProject(false), "The folder's saved Graph project could not close.");
    std::cout << "PASS first Graph save stays in the loaded folder and appears in Projects\n";
}
}
