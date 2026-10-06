#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketCoordinator.h"
#include "Editor/RawRenderService.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/BracketingProject.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace Stack::Validation {
namespace {
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Save { unsigned calls = 0; bool success = false; };
}

// Uses two existing small RAW files and public editing operations. All writes
// go to new scratch projects; source files remain read-only.
bool ValidateConcurrentBracketing(const std::filesystem::path& sourceA,
    const std::filesystem::path& sourceB) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(640, 480, "Concurrent bracket validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {640, 480};
    unsigned char* pixels; int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Async::TaskSystem::Get().Initialize();
    auto a = std::make_unique<EditorModule>();
    auto b = std::make_unique<EditorModule>();
    auto editing = std::make_unique<EditorModule>();
    AutoBracket::AutoBracketCoordinator automatic;
    bool success = false;
    try {
        const auto root = std::filesystem::current_path() / "_workspace" /
            ("concurrent-brackets-" + Project::GenerateStableUuid());
        std::filesystem::create_directories(root);
        for (auto* editor : {a.get(), b.get(), editing.get()}) {
            editor->Initialize(window, nullptr, false);
            editor->SetWorkspaceAppStatePersistenceEnabled(false);
            editor->SetRawWorkspaceFolder(root);
        }
        const auto executor = [](Raw::OpenGlTask task, std::string& error) {
            return EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(std::move(task), error);
        };
        const auto frame = [&] {
            ImGui::NewFrame();
            for (auto* editor : {a.get(), b.get(), editing.get()}) {
                editor->PumpNonRenderingWork(3, editor == editing.get());
                editor->TickBracketing(editor == editing.get());
                editor->UpdateBracketingPresentation(false);
            }
            automatic.Tick(ImGui::GetTime(), true, false, executor);
            ImGui::EndFrame();
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        const auto wait = [&](auto ready, const char* message) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(90);
            while (!ready() && std::chrono::steady_clock::now() < until) frame();
            Check(ready(), message);
        };
        const std::vector<std::filesystem::path> sources{
            std::filesystem::absolute(sourceA), std::filesystem::absolute(sourceB)};
        for (auto* editor : {a.get(), b.get()}) {
            editor->EnterRawWorkspaceRootTab();
            editor->BeginBracketingDraft(true);
            editor->AddBracketingDraftFiles(sources);
        }
        bool importedA = false, importedB = false;
        wait([&] {
            for (auto pair : {std::make_pair(a.get(), &importedA), std::make_pair(b.get(), &importedB)}) {
                if (*pair.second) continue;
                std::string error;
                *pair.second = pair.first->CommitBracketingDraft(false, &error);
                if (!*pair.second) Check(error == "Wait for capture metadata inspection to finish.", error);
            }
            return importedA && importedB;
        }, "Draft import timed out.");
        wait([&] { return a->HasDeferredLoadedProjectApplyCoreFinished() &&
            b->HasDeferredLoadedProjectApplyCoreFinished(); }, "Independent project activation timed out.");
        frame();
        const auto pathA = a->GetCurrentProjectFileName(), pathB = b->GetCurrentProjectFileName();
        Check(pathA != pathB && a->GetProjectDocumentId() != b->GetProjectDocumentId(),
            "Concurrent brackets reused a destination or identity.");
        const auto snapshot = *a->GetActiveRawProjectSnapshot();
        AutoBracket::Candidate candidate;
        candidate.name = "Automatic bracket";
        for (const auto& asset : snapshot.embeddedAssets)
            candidate.sources.push_back({asset.originalSourcePath, asset.sha256, asset.byteLength});
        candidate.identity = AutoBracket::SourceIdentity(candidate.sources);
        Check(!candidate.identity.empty() && automatic.SetRoot(root / "automatic", 0), "Automatic queue setup failed.");
        automatic.UpdateCandidates({candidate}, {});
        std::string error;
        Check(a->CommitBracketingDraft(true, &error), error);
        Check(b->CommitBracketingDraft(true, &error), error);
        automatic.Tick(0, true, false, executor);
        Check(automatic.HasWork(), "Automatic queue waited for another tab.");
        automatic.SetPaused(true);
        double progress; std::string stage;
        Check(a->GetActiveBracketingProcessingDiagnostic(progress, stage) &&
            b->GetActiveBracketingProcessingDiagnostic(progress, stage), "Two manual bracket jobs did not coexist.");
        a->CancelBracketingPresentation();
        editing->SetCurrentProjectName("Editing during brackets");
        editing->AddReformatNodeAt({10, 20});
        editing->AddOutputNodeAt({200, 20});
        auto editSave = std::make_shared<Save>();
        Check(editing->RequestSaveCurrentProject("Editing during brackets", [editSave](bool ok) {
            ++editSave->calls; editSave->success = ok;
        }), "A bracket blocked another project's save.");
        wait([&] { return !a->GetActiveBracketingProcessingDiagnostic(progress, stage) &&
            !b->GetActiveBracketingProcessingDiagnostic(progress, stage) && !automatic.HasWork() && editSave->calls; },
            "Concurrent processing or saving timed out.");
        Check(editSave->calls == 1 && editSave->success, "Independent edit save failed.");
        Check(!b->DidActiveMultiFrameProcessingForQueueFail(&error), error);
        const auto completed = automatic.ConsumeCompleted();
        Check(completed && completed->outcome == AutoBracket::State::Completed,
            completed ? completed->status : "Automatic completion was lost.");
        Check(!a->IsActiveMultiFrameProcessingForQueueBusy(), "Canceled bracket did not stop.");
        Check(a->StartBracketingProcessing(true, &error), error);
        wait([&] { return !a->GetActiveBracketingProcessingDiagnostic(progress, stage); }, "Canceled bracket could not restart.");
        Check(!a->DidActiveMultiFrameProcessingForQueueFail(&error), error);
        auto savedA = std::make_shared<Save>(), savedB = std::make_shared<Save>();
        Check(a->RequestSaveCurrentProject("Manual A", [savedA](bool ok) { ++savedA->calls; savedA->success = ok; }), "Save A rejected.");
        Check(b->RequestSaveCurrentProject("Manual B", [savedB](bool ok) { ++savedB->calls; savedB->success = ok; }), "Save B rejected.");
        wait([&] { return savedA->calls && savedB->calls; }, "Overlapping bracket saves timed out.");
        Check(savedA->calls == 1 && savedA->success && savedB->calls == 1 && savedB->success, "Overlapping bracket saves failed.");
        for (const auto& path : {a->GetCurrentProjectFileName(), b->GetCurrentProjectFileName(), completed->item.projectPath.string()}) {
            const auto opened = Project::OpenProjectStore(path);
            Check(bool(opened), opened.message);
            Raw::Bracketing::BracketingResult restored;
            Check(Project::RestoreBracketingResult(opened.store, opened.snapshot,
                opened.snapshot.activeSourceSetId, restored, error), error);
            Check(bool(restored.raw), "Saved bracket lost its result.");
        }
        Check(a->GetProjectDocumentId() != b->GetProjectDocumentId() &&
            a->GetCurrentProjectName() == "Manual A" && b->GetCurrentProjectName() == "Manual B" &&
            editing->GetCurrentProjectName() == "Editing during brackets", "Project names or identities crossed tabs.");
        std::cout << "PASS two manual brackets plus an automatic bracket, independent cancellation/restart, editing, overlapping saves and result reopen\n";
        std::cout << "Validation projects: " << root << '\n';
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "Concurrent bracket validation failed: " << error.what() << '\n';
    }
    automatic.Shutdown();
    for (auto* editor : {a.get(), b.get(), editing.get()}) editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();
    editing.reset(); b.reset(); a.reset();
    ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return success;
}
}
