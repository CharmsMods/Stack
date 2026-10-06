#include "App/Validation/ValidationSuites.h"
#include "App/Validation/Suites/HeaderActionRegistryValidation.h"
#include "App/Validation/Suites/ProjectBracketingOwnershipValidation.h"
#include "App/Validation/Suites/ProjectFileOperationsValidation.h"
#include "App/Validation/Suites/ProjectGraphSnapshotValidation.h"
#include "App/Validation/Suites/BracketingPresentationValidation.h"
#include "App/ProjectWorkspace.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketCoordinator.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Renderer/GLLoader.h"

#include <filesystem>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
namespace {
void RequireWorkspace(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ValidateProjectLoadTransitionOwnership() {
    ProjectLoadTransition a, b;
    Async::TaskGroup tasksA, tasksB;
    a.Reset();
    b.Reset();
    a.phase = b.phase = ProjectLoadPhase::SpinnerFadeIn;
    a.ownerLease = tasksA.Retain();
    b.ownerLease = tasksB.Retain();
    b.projectFileName = "project-b.stack";
    const auto generationA = a.generation;
    const auto generationB = b.generation;
    RequireWorkspace(a.AcceptsCompletion(generationA) && b.AcceptsCompletion(generationB) &&
        !tasksA.IsIdle() && !tasksB.IsIdle(), "Loading did not retain its owning project.");
    a.Reset();
    RequireWorkspace(tasksA.IsIdle() && !tasksB.IsIdle() &&
        !a.AcceptsCompletion(generationA) && b.AcceptsCompletion(generationB) &&
        b.projectFileName == "project-b.stack" && b.phase == ProjectLoadPhase::SpinnerFadeIn,
        "Retiring a load retained its owner or changed another project's transition.");
    a.phase = ProjectLoadPhase::SpinnerFadeIn;
    RequireWorkspace(!a.AcceptsCompletion(generationA) && a.AcceptsCompletion(a.generation),
        "A late completion was accepted by a replacement load.");
    b.Reset();
    RequireWorkspace(tasksB.IsIdle(), "Resetting a load retained its project task lease.");
    std::cout << "PASS project load transition leases and stale completion ownership\n";
}

void ValidateProjectSessionOwnership() {
    Project::ProjectSession a, b;
    a.name = "A";
    b.name = "B";
    a.graph.AddReformatNode({40, 40});
    a.rawRecipe = RawRecipe::MakeDefaultRecipe("camera-a.raw");
    b.rawRecipe = RawRecipe::MakeDefaultRecipe("camera-b.raw");
    const auto bRecipe = RawRecipe::SerializeRecipe(b.rawRecipe);
    a.NoteEdit(10.0);
    b.EnsureDocumentId();
    const auto savedRevision = a.editRevision;
    a.files->save.Begin(a.documentId, savedRevision);
    a.NoteEdit(11.0);
    RequireWorkspace(!a.ClearDirtyIfRevision(savedRevision) && a.dirty &&
        !b.dirty && b.editRevision == 0 && b.graph.GetNodes().empty() &&
        a.documentId != b.documentId && b.name == "B" &&
        RawRecipe::SerializeRecipe(b.rawRecipe) == bRecipe &&
        b.files->save.state == Async::TaskState::Idle &&
        a.files != b.files,
        "Project data or save revision crossed independent headless sessions.");
    RequireWorkspace(a.ClearDirtyIfRevision(a.editRevision) && !a.dirty,
        "The current project revision could not be acknowledged.");
    std::cout << "PASS independent project data and save revisions without editor construction\n";
}

nlohmann::json WorkspaceGraph(const EditorModule& editor) {
    return EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), editor.GetNodeGraph());
}

void ValidateWorkspaceHistoryReplacement(EditorModule& editor, const EditorModule& other) {
    const auto otherGraph = WorkspaceGraph(other);
    const bool otherDirty = other.IsDirty();
    const bool otherUndo = other.CanUndoFrequencyGraphAction();
    const bool otherRedo = other.CanRedoFrequencyGraphAction();
    const auto addHistory = [&] {
        const auto* filter = editor.GetNodeGraph().AddFrequencyFilterNode(
            EditorNodeGraph::FrequencyFilterMode::BandStop, {240, 240});
        RequireWorkspace(filter != nullptr, "Could not create the history check's filter.");
        std::string error;
        RequireWorkspace(editor.ExtractFrequencyResponseNode(filter->id, &error),
            "Could not create frequency history through the editor action.");
        RequireWorkspace(editor.CanUndoFrequencyGraphAction(),
            "The frequency action did not record undo history.");
    };

    addHistory();
    RequireWorkspace(editor.CloseCurrentProject(true) &&
        !editor.CanUndoFrequencyGraphAction() && !editor.CanRedoFrequencyGraphAction(),
        "Closing a project retained its frequency history.");

    addHistory();
    RequireWorkspace(editor.UndoFrequencyGraphAction() && editor.CanRedoFrequencyGraphAction(),
        "The frequency action did not record redo history.");
    const auto replacement = editor.SerializePipeline();
    editor.DeserializePipeline(replacement);
    RequireWorkspace(!editor.CanUndoFrequencyGraphAction() && !editor.CanRedoFrequencyGraphAction(),
        "Replacing the pipeline retained the prior document's frequency history.");
    RequireWorkspace(editor.CloseCurrentProject(true), "Could not close the history check's project.");
    RequireWorkspace(WorkspaceGraph(other) == otherGraph && other.IsDirty() == otherDirty &&
        other.CanUndoFrequencyGraphAction() == otherUndo && other.CanRedoFrequencyGraphAction() == otherRedo,
        "Replacing one project's history changed another workspace.");
    std::cout << "PASS close and pipeline replacement clear only the owning project's undo/redo\n";
}

void RenderRawWorkspaceValidationFrame(EditorModule& editor) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({1000, 700});
    ImGui::Begin("Workspace validation", nullptr,
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
    editor.RenderRawWorkspaceLabUI();
    ImGui::End();
    ImGui::EndFrame();
    Async::TaskSystem::Get().PumpMainThreadTasks(4);
}

void SaveWorkspaceAndWait(EditorModule& editor) {
    struct Completion { bool done = false, success = false; };
    const auto completion = std::make_shared<Completion>();
    RequireWorkspace(editor.RequestSaveWorkspaceBeforeClose([completion](bool success) {
        completion->success = success;
        completion->done = true;
    }), "The workspace save request was rejected.");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!completion->done && std::chrono::steady_clock::now() < deadline) {
        ImGui::NewFrame();
        editor.PumpNonRenderingWork(2.5);
        ImGui::EndFrame();
        Async::TaskSystem::Get().PumpMainThreadTasks(4);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!completion->done || !completion->success) {
        std::cerr << "Workspace save status for " << editor.GetCurrentProjectName() << ": "
            << editor.GetProjectFileSaveStatusText() << '\n';
        throw std::runtime_error(completion->done
            ? "The workspace save callback reported failure."
            : "The workspace save did not finish within 15 seconds.");
    }
}

std::filesystem::path WorkspaceSaveScratchDirectory() {
    auto root = std::filesystem::current_path();
    while (!root.empty()) {
        if (std::filesystem::is_directory(root / "Guidance") &&
            std::filesystem::is_directory(root / "src")) {
            const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto scratch = root / "_workspace" /
                ("project-workspace-save-validation-" + std::to_string(serial));
            std::filesystem::create_directories(scratch);
            return scratch;
        }
        const auto parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    throw std::runtime_error("Run workspace save validation from inside the Stack repository.");
}

void ValidateAutoBracketWorkspaceLifecycle(EditorModule& donor, GLFWwindow* window,
    const std::filesystem::path& scratch) {
    using namespace AutoBracket;
    // Queue metadata is sufficient to check dispatch eligibility and persisted
    // progress. This check never submits an image-processing job.
    Queue saved;
    saved.root = scratch / "auto-bracket-queue";
    saved.items.push_back({{"saved-bracket", "Saved bracket", {}}, State::Completed, "saved-project"});
    saved.items.push_back({{"pending-bracket", "Pending bracket", {}}, State::Pending, "pending-project"});
    saved.items.back().projectPath = saved.root / "pending-project";
    std::string error;
    RequireWorkspace(SaveQueue(saved, error), "Could not write the queue metadata check.");
    const std::vector<Candidate> candidates{saved.items[0].candidate, saved.items[1].candidate};
    AutoBracketCoordinator scheduler;
    RequireWorkspace(scheduler.SetRoot(saved.root, 0), "Could not load the queue metadata check.");
    scheduler.UpdateCandidates(candidates, {});
    RequireWorkspace(scheduler.CanStartWork(0, true, false) &&
        scheduler.CanStartWork(60, true, false) &&
        !scheduler.CanStartWork(60, false, false) && !scheduler.CanStartWork(60, true, true),
        "Automatic dispatch did not start when ready or ignored the shutdown gate.");
    scheduler.NoteActivity(10);
    RequireWorkspace(scheduler.CanStartWork(10, true, false),
        "User activity delayed an automatic queue that had not been stopped.");
    scheduler.SetProtectedProjectIds({"saved-project", "pending-project"});
    RequireWorkspace(!scheduler.CanStartWork(60, true, false),
        "An open project remained eligible for background processing.");
    scheduler.SetProtectedProjectIds({});
    scheduler.SetProtectedProjectIds({}, {saved.items.back().projectPath / "project.stack"});
    RequireWorkspace(!scheduler.CanStartWork(60, true, false),
        "A project being opened remained eligible for automatic writes.");
    scheduler.SetProtectedProjectIds({});
    scheduler.CancelCurrent();
    scheduler.NoteActivity(61);
    scheduler.Shutdown();
    AutoBracketCoordinator reopened;
    RequireWorkspace(reopened.SetRoot(saved.root, 61), "Could not reopen the bracket queue.");
    reopened.UpdateCandidates(candidates, {});
    RequireWorkspace(!reopened.GetQueue().paused && reopened.GetQueue().items[0].state == State::Completed &&
        reopened.GetQueue().items[1].projectId == "pending-project" &&
        reopened.CanStartWork(61, true, false),
        "Reopening lost saved progress, project identity, or immediate eligibility.");
    reopened.PauseUntilIdle(100);
    RequireWorkspace(!reopened.CanStartWork(159, true, false) && reopened.CanStartWork(160, true, false),
        "A user stop did not require one minute of inactivity.");
    reopened.NoteActivity(150);
    RequireWorkspace(!reopened.CanStartWork(209, true, false) && reopened.CanStartWork(210, true, false),
        "Further editing did not restart the idle wait.");
    AutoBracketCoordinator idleReopened;
    RequireWorkspace(idleReopened.SetRoot(saved.root, 160), "Could not reload the idle pause.");
    idleReopened.UpdateCandidates(candidates, {});
    RequireWorkspace(idleReopened.GetQueue().resumeWhenIdle &&
        !idleReopened.CanStartWork(219, true, false) && idleReopened.CanStartWork(220, true, false),
        "Closing and reopening the queue lost its idle pause.");
    idleReopened.RunNow();
    RequireWorkspace(!idleReopened.GetQueue().paused && !idleReopened.GetQueue().resumeWhenIdle &&
        idleReopened.CanStartWork(160, true, false), "Run now did not override the idle wait.");
    idleReopened.Shutdown();
    reopened.SetPaused(true);
    AutoBracketCoordinator paused;
    RequireWorkspace(paused.SetRoot(saved.root, 62) && paused.GetQueue().paused &&
        !paused.CanStartWork(1000, true, false), "An explicit pause was lost on reload.");
    paused.Shutdown();
    reopened.SetPaused(false);
    reopened.Exclude("pending-bracket");
    RequireWorkspace(!reopened.CanStartWork(121, true, false),
        "Automatic processing requested a workspace with no eligible brackets.");
    reopened.Shutdown();

    const auto graph = WorkspaceGraph(donor);
    auto worker = std::make_unique<EditorModule>();
    worker->SetDocumentPersistenceEnabled(false);
    worker->Initialize(window, nullptr, false);
    donor.SetDocumentPersistenceEnabled(true);
    ImGui::NewFrame();
    donor.TickAutoBracketing(true);
    ImGui::EndFrame();
    RequireWorkspace(!donor.ConsumeAutoBracketWorkspaceRequest() &&
        donor.TransferAutoBracketingTo(*worker) && !donor.IsAutoBracketWorkspace() &&
        worker->IsAutoBracketWorkspace(), "The automatic queue did not transfer into its own workspace.");
    donor.SetDocumentPersistenceEnabled(false);
    bool takeoverRan = false;
    RequireWorkspace(!worker->RequestAutoBracketForeground("change project tabs", [&] { takeoverRan = true; }) &&
        !takeoverRan, "Switching from the automatic workspace requested a takeover.");
    ImGui::NewFrame();
    ImGui::Begin("Automatic workspace validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
    worker->RenderAutoBracketWorkspace();
    ImGui::End();
    worker->CancelAutoBracketingForWorkspaceClose();
    worker->TickAutoBracketing(false);
    ImGui::EndFrame();
    RequireWorkspace(!worker->AutoBracketWorkActive() && !worker->ConsumeAutoBracketWorkspaceRequest() &&
        WorkspaceGraph(donor) == graph, "Closing automatic processing changed the donor or restarted work.");
    worker->Shutdown();
    donor.ShutdownAutoBracketing();
    std::cout << "PASS automatic workspace transfer, immediate scheduling, idle resume, and durable pause\n"
        << "SKIP active bracket cancellation and pixel output: no image-processing job in this focused check\n";
}
}

bool ValidateProjectWorkspaces(const std::string& projectPath) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(1000, 700, "Project workspace validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {1000, 700};
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0, fontHeight = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    Async::TaskSystem::Get().Initialize();
    auto first = std::make_unique<ProjectWorkspace>(1);
    auto second = std::make_unique<ProjectWorkspace>(2);
    bool success = false;
    try {
        ValidateProjectSessionOwnership();
        ValidateProjectGraphSnapshotBuilder();
        ValidateProjectBracketingOwnership();
        ValidateProjectLoadTransitionOwnership();
        BracketingPresentationValidationAccess::ValidateProjectOwnership();
        RequireWorkspace(ValidateHeaderActionRegistry(), "Header action registry validation failed.");
        for (auto* workspace : {first.get(), second.get()}) {
            workspace->editor->SetDocumentPersistenceEnabled(false);
            workspace->editor->Initialize(window, nullptr, false);
        }
        auto& a = *first->editor;
        auto& b = *second->editor;
        const auto blankGraph = WorkspaceGraph(a);
        RequireWorkspace(!a.IsDirty() && !b.IsDirty() &&
            a.GetCurrentProjectName().empty() && b.GetCurrentProjectName().empty() &&
            a.GetRawWorkspaceState().workspaceRoot.empty() && b.GetRawWorkspaceState().workspaceRoot.empty() &&
            a.GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty &&
            b.GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty &&
            WorkspaceGraph(b) == blankGraph,
            "New project workspaces did not start clean.");
        std::cout << "PASS new workspaces have independent, empty project state\n";

        RequireWorkspace(!a.IsRawWorkspaceGalleryWorkspaceOpen() &&
            !b.IsRawWorkspaceGalleryWorkspaceOpen(),
            "A project workspace unexpectedly opened the permanent Gallery.");
        // The shell explicitly marks its one permanent Gallery editor. Project
        // editors start separately and navigate there through a shell handler.
        a.SetPermanentGalleryWorkspace(true);
        RequireWorkspace(a.EnterRawWorkspaceRootTab() &&
            a.IsRawWorkspaceGalleryAvailable() && a.IsRawWorkspaceGalleryWorkspaceOpen(),
            "An empty RAW workspace did not start in Gallery.");
        RenderRawWorkspaceValidationFrame(a);
        RenderRawWorkspaceValidationFrame(a);
        a.ToggleRawWorkspaceGallery();
        RenderRawWorkspaceValidationFrame(a);
        RequireWorkspace(a.IsRawWorkspaceGalleryWorkspaceOpen(),
            "An empty workspace's Gallery button dismissed its folder picker.");
        RequireWorkspace(a.LeaveRawWorkspaceRootTab(true) && a.EnterRawWorkspaceRootTab(),
            "The empty workspace could not return to RAW.");
        RenderRawWorkspaceValidationFrame(a);
        RequireWorkspace(a.IsRawWorkspaceGalleryWorkspaceOpen() && !a.IsDirty() &&
            WorkspaceGraph(a) == blankGraph && !b.IsDirty() && WorkspaceGraph(b) == blankGraph,
            "Returning to empty Gallery changed project state or lost Gallery mode.");
        RequireWorkspace(a.LeaveRawWorkspaceRootTab(true), "Could not leave empty Gallery.");
        a.SetPermanentGalleryWorkspace(false);
        std::cout << "PASS empty RAW Gallery renders, retains its folder picker, and survives leaving RAW\n";

        a.SetCurrentProjectName("Workspace A");
        a.GetNodeGraph().AddReformatNode({20, 40});
        a.MarkDirty();
        const auto firstGraph = WorkspaceGraph(a);
        RequireWorkspace(b.GetCurrentProjectName().empty() && !b.IsDirty() &&
            WorkspaceGraph(b) == blankGraph, "Editing the first workspace changed the second.");
        b.SetCurrentProjectName("Workspace B");
        b.GetNodeGraph().AddReformatNode({80, 120});
        b.MarkDirty();
        const auto secondGraph = WorkspaceGraph(b);
        a.ClearDirty();
        RequireWorkspace(!a.IsDirty() && b.IsDirty() &&
            a.GetCurrentProjectName() == "Workspace A" &&
            b.GetCurrentProjectName() == "Workspace B" &&
            WorkspaceGraph(a) == firstGraph && firstGraph != secondGraph,
            "Names, graph edits, or dirty flags leaked between workspaces.");
        a.MarkDirty();
        RequireWorkspace(!a.CloseCurrentProject(false) && a.IsDirty() &&
            WorkspaceGraph(a) == firstGraph, "Close without discard lost a dirty project.");
        RequireWorkspace(a.CloseCurrentProject(true) && !a.IsDirty() &&
            a.GetCurrentProjectName().empty() && WorkspaceGraph(a) == blankGraph &&
            b.IsDirty() && b.GetCurrentProjectName() == "Workspace B" &&
            WorkspaceGraph(b) == secondGraph, "Discard did not close only its owning workspace.");
        std::cout << "PASS names, graph edits, dirty flags, and discard remain in their own workspace\n";

        ValidateWorkspaceHistoryReplacement(a, b);

        const auto scratch = WorkspaceSaveScratchDirectory();
        a.SetDocumentPersistenceEnabled(true);
        RequireWorkspace(!a.NeedsWorkspaceSaveBeforeTransition(),
            "An empty workspace requested a document save.");
        SaveWorkspaceAndWait(a);
        RequireWorkspace(a.GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty &&
            a.GetCurrentProjectFileName().empty() && std::filesystem::is_empty(scratch),
            "Saving an empty workspace created a project.");
        std::cout << "PASS empty workspace save completes without a project\n";

        // The default project-library path has no public override. Use the
        // normal public destination setter to keep real save artifacts inside
        // the repository; automatic destination selection is not exercised.
        const auto savedPath = scratch / "Workspace save check";
        a.SetCurrentProjectName("Workspace save check");
        a.SetCurrentProjectFileName(savedPath.string());
        a.AddReformatNodeAt({20, 40});
        // Ordinary projects retain an Output node. Reopening a graph that
        // omits it intentionally repairs that graph, so include it in this
        // save test instead of comparing before/after repair states.
        a.AddOutputNodeAt({360, 40});
        RequireWorkspace(a.IsDirty() && a.NeedsWorkspaceSaveBeforeTransition() &&
            !std::filesystem::exists(savedPath), "The first save was not an unsaved document.");
        SaveWorkspaceAndWait(a);
        RequireWorkspace(!a.IsDirty() && !a.NeedsWorkspaceSaveBeforeTransition() &&
            std::filesystem::exists(savedPath), "The first workspace save did not create its project.");
        const auto firstSaved = Project::ProjectOpenCoordinator::Load(savedPath);
        RequireWorkspace(static_cast<bool>(firstSaved), "The first saved project could not be read back.");
        std::cout << "PASS first ordinary project save and readback\n";

        a.AddReformatNodeAt({180, 120});
        const auto editedGraph = WorkspaceGraph(a);
        RequireWorkspace(a.NeedsWorkspaceSaveBeforeTransition(), "A subsequent edit did not need saving.");
        SaveWorkspaceAndWait(a);
        RequireWorkspace(!a.IsDirty() && a.CloseCurrentProject(false),
            "The workspace did not close after its subsequent durable save.");
        const auto reopened = Project::ProjectOpenCoordinator::Load(savedPath);
        RequireWorkspace(static_cast<bool>(reopened), "The saved verification project could not be opened.");
        RequireWorkspace(a.ApplyLoadedProject(*reopened.candidate),
            "The saved verification project could not be applied to the workspace.");
        const auto reopenedGraph = WorkspaceGraph(a);
        if (reopenedGraph != editedGraph)
            std::cerr << "Saved workspace graph difference: "
                << nlohmann::json::diff(editedGraph, reopenedGraph).dump(2) << '\n';
        RequireWorkspace(reopenedGraph == editedGraph,
            "The subsequent save did not retain the edited graph after close and reopen.");
        RequireWorkspace(b.IsDirty() && b.GetCurrentProjectName() == "Workspace B" &&
            b.GetCurrentProjectFileName().empty() && WorkspaceGraph(b) == secondGraph,
            "Saving or closing one workspace changed the other workspace.");
        RequireWorkspace(a.CloseCurrentProject(false), "The saved verification project could not close.");
        a.SetDocumentPersistenceEnabled(false);
        std::cout << "PASS first save, subsequent save, and close preserve independent workspaces\n"
            << "Save validation artifacts: " << scratch.string() << '\n'
            << "SKIP automatic project-library destination selection: no public path override\n";

        // The app distributes one folder to each editor through this public
        // setter. Preferences stay disabled for both verification editors.
        const auto sharedFolder = (scratch / "empty-folder").lexically_normal();
        std::filesystem::create_directories(sharedFolder);
        a.SetCurrentProjectName("Folder inheritance check");
        a.AddReformatNodeAt({120, 40});
        const auto folderProjectGraph = WorkspaceGraph(a);
        a.SetRawWorkspaceFolder(sharedFolder);
        b.SetRawWorkspaceFolder(sharedFolder);
        RequireWorkspace(a.GetRawWorkspaceState().workspaceRoot.lexically_normal() == sharedFolder &&
            b.GetRawWorkspaceState().workspaceRoot.lexically_normal() == sharedFolder,
            "Two workspaces did not inherit the shared RAW folder.");
        RequireWorkspace(a.IsDirty() && a.GetCurrentProjectName() == "Folder inheritance check" &&
            WorkspaceGraph(a) == folderProjectGraph && b.IsDirty() &&
            b.GetCurrentProjectName() == "Workspace B" && WorkspaceGraph(b) == secondGraph,
            "Setting the shared RAW folder changed a workspace's document.");
        RequireWorkspace(a.CloseCurrentProject(true) &&
            a.GetRawWorkspaceState().workspaceRoot.lexically_normal() == sharedFolder &&
            b.GetRawWorkspaceState().workspaceRoot.lexically_normal() == sharedFolder &&
            WorkspaceGraph(a) == blankGraph && !a.IsDirty() &&
            b.IsDirty() && WorkspaceGraph(b) == secondGraph,
            "Closing a document cleared the shared RAW folder or changed another workspace.");
        std::cout << "PASS shared RAW folder inheritance preserves documents and survives document close\n";

        ValidateAutoBracketWorkspaceLifecycle(a, window, scratch);

        if (!projectPath.empty()) {
            const auto path = std::filesystem::absolute(projectPath);
            RequireWorkspace(std::filesystem::is_regular_file(path), "The supplied project is not an existing file.");
            const auto sizeBefore = std::filesystem::file_size(path);
            const auto writeBefore = std::filesystem::last_write_time(path);
            const auto opened = Project::ProjectOpenCoordinator::Load(path);
            if (!opened) throw std::runtime_error(opened.error);
            RequireWorkspace(a.ApplyLoadedProject(*opened.candidate), "The supplied project could not be applied.");
            a.EnterRawWorkspaceRootTab();
            const auto frame = [&] { RenderRawWorkspaceValidationFrame(a); };
            // Transform is available even when a saved bracket has no processed result.
            a.RequestRawLabToolIndex(0);
            frame();
            RequireWorkspace(a.GetRawLabToolIndex() == 0, "Could not select the Edit workspace's Transform tool.");
            if (a.IsRawWorkspaceGalleryAvailable()) {
                const auto graphBeforeGallery = WorkspaceGraph(a);
                const auto projectBeforeGallery = a.GetCurrentProjectFileName();
                const bool dirtyBeforeGallery = a.IsDirty();
                auto galleryRequests = std::make_shared<int>(0);
                a.SetGalleryNavigationHandler([galleryRequests] { ++*galleryRequests; });
                a.CloseRawWorkspaceGalleryWorkspace();
                a.ToggleRawWorkspaceGallery();
                frame();
                RequireWorkspace(*galleryRequests == 1 && !a.IsRawWorkspaceGalleryWorkspaceOpen() &&
                    a.GetRawLabToolIndex() == 0 && WorkspaceGraph(a) == graphBeforeGallery &&
                    a.GetCurrentProjectFileName() == projectBeforeGallery && a.IsDirty() == dirtyBeforeGallery,
                    "Gallery toggle did not route navigation while preserving its project and editing tool.");
                RequireWorkspace(a.OpenRawWorkspaceGalleryWorkspace(), "The Gallery navigation request was rejected.");
                frame();
                RequireWorkspace(*galleryRequests == 2 && !a.IsRawWorkspaceGalleryWorkspaceOpen() &&
                    a.GetRawLabToolIndex() == 0 && WorkspaceGraph(a) == graphBeforeGallery &&
                    a.GetCurrentProjectFileName() == projectBeforeGallery && a.IsDirty() == dirtyBeforeGallery,
                    "Opening Gallery changed its project or editing tool instead of routing navigation.");
                a.SetGalleryNavigationHandler({});
                std::cout << "PASS dedicated Gallery navigation preserves the project and selected Edit tool\n";
            } else {
                std::cout << "SKIP Gallery toggle: the supplied project has no RAW folder through its public state\n";
            }
            a.RequestRawBracketMode(true);
            frame();
            RequireWorkspace(a.IsRawBracketModeActive(), "The Bracket mode request was not applied.");
            a.RequestRawBracketMode(false);
            frame();
            RequireWorkspace(!a.IsRawBracketModeActive() && a.GetRawLabToolIndex() == 0,
                "Edit did not restore the tool selected before Bracket.");
            RequireWorkspace(b.IsDirty() && b.GetCurrentProjectName() == "Workspace B" &&
                WorkspaceGraph(b) == secondGraph, "RAW navigation changed another workspace.");
            RequireWorkspace(std::filesystem::file_size(path) == sizeBefore &&
                std::filesystem::last_write_time(path) == writeBefore,
                "The supplied project's file changed during read-only validation.");
            std::cout << "PASS Bracket/Edit restores the previous tool; the supplied project file is unchanged\n";
        } else {
            std::cout << "SKIP RAW navigation checks: supply an existing .stack project to enable them\n";
        }
        ValidateOverlappingProjectFileOperations(a, b, scratch);
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "Project workspace validation failed: " << error.what() << '\n';
    }
    first->editor->RequestWorkerShutdownForAppClose();
    second->editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();
    first.reset();
    second.reset();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return success;
}
} // namespace Stack::Validation
