#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketCoordinator.h"
#include "Editor/AutoBracket/AutoBracketPresentation.h"
#include "App/AppHeaderStyle.h"
#include "App/settings/AppearanceTheme.h"
#include "Editor/RawRenderService.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/BracketingProject.h"
#include "Persistence/ProjectIndex.h"
#include "Project/BracketingJobRunner.h"
#include "Raw/RawGallerySimilarity.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <set>
#include <thread>

namespace Stack::Validation {
namespace {
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Save { unsigned calls = 0; bool success = false; };
void CaptureConcurrentFrame(const std::filesystem::path& path, bool blackCorners = false) {
    constexpr int width = 1440, height = 900;
    std::vector<unsigned char> bytes(std::size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    if (blackCorners) for (const auto corner : {0u, unsigned(width - 1), unsigned((height - 1) * width), unsigned(height * width - 1)})
        Check(bytes[corner * 4] <= 1 && bytes[corner * 4 + 1] <= 1 && bytes[corner * 4 + 2] <= 1,
            "The fullscreen processing surface did not cover every corner with black.");
    for (int y = 0; y < height / 2; ++y) for (int x = 0; x < width * 4; ++x)
        std::swap(bytes[std::size_t(y) * width * 4 + x], bytes[std::size_t(height - y - 1) * width * 4 + x]);
    Check(stbi_write_png(path.string().c_str(), width, height, 4, bytes.data(), width * 4) != 0,
        "Could not save the native concurrent-processing capture.");
}
}

// Uses two existing RAW files and public editing operations. All writes
// go to new scratch projects; source files remain read-only.
bool ValidateConcurrentBracketing(const std::filesystem::path& sourceA,
    const std::filesystem::path& sourceB) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(1440, 900, "Concurrent bracket validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    StackAppearance::AppearanceManager appearance;
    appearance.Load();
    appearance.SetupFonts(ImGui::GetIO());
    appearance.ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 430");
    Async::TaskSystem::Get().Initialize();
    auto a = std::make_unique<EditorModule>();
    auto b = std::make_unique<EditorModule>();
    auto editing = std::make_unique<EditorModule>();
    AutoBracket::AutoBracketCoordinator automatic;
    AutoBracket::Presentation automaticScene;
    bool success = false;
    try {
        const auto root = std::filesystem::current_path() / "_workspace" /
            ("concurrent-brackets-" + Project::GenerateStableUuid());
        const auto originals = root / "originals";
        std::filesystem::create_directories(originals);
        std::cout << "Validation projects: " << root << '\n' << std::flush;
        std::vector<std::filesystem::path> sources;
        for (const auto& input : {sourceA, sourceB}) {
            const auto destination = originals / input.filename();
            Check(std::find(sources.begin(), sources.end(), destination) == sources.end(),
                "Concurrent validation inputs need distinct filenames.");
            std::filesystem::copy_file(input, destination);
            sources.push_back(destination);
        }
        for (auto* editor : {a.get(), b.get(), editing.get()}) {
            editor->Initialize(window, &appearance, false);
            editor->SetWorkspaceAppStatePersistenceEnabled(false);
            editor->SetRawWorkspaceFolder(root);
        }
        const auto executor = [](Raw::OpenGlTask task, std::string& error) {
            return EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(
                std::move(task), error, Raw::OpenGlTaskPriority::Background);
        };
        bool nativeProcessingCaptured = false;
        bool observedConcurrentWork = false;
        double nativeImagesShownAt = -1;
        const auto frame = [&] {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            for (auto* editor : {a.get(), b.get(), editing.get()}) {
                editor->PumpNonRenderingWork(3, editor == editing.get());
                editor->TickBracketing(editor == editing.get());
                editor->UpdateBracketingPresentation(false);
            }
            automatic.Tick(ImGui::GetTime(), true, false, executor);
            if (automatic.HasWork()) {
                double progress = 0; std::string stage;
                observedConcurrentWork |= a->GetActiveBracketingProcessingDiagnostic(progress, stage) &&
                    b->GetActiveBracketingProcessingDiagnostic(progress, stage);
            }
            bool captureThisFrame = false;
            if (!nativeProcessingCaptured) if (const auto current = automatic.Current()) {
                if (current->generation != automaticScene.generation)
                    automaticScene.Begin(current, ImGui::GetTime());
                const auto publication = current->mailbox->Read();
                if (!current->done && publication && !publication->images.empty()) {
                    if (nativeImagesShownAt < 0) nativeImagesShownAt = ImGui::GetTime();
                    AutoBracket::DrawPresentation(automaticScene, automatic);
                    captureThisFrame = ImGui::GetTime() - nativeImagesShownAt >= .35;
                }
            }
            ImGui::Render();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, 1440, 900);
            const auto background = appearance.GetClearColor();
            glClearColor(background.x, background.y, background.z, background.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (captureThisFrame) {
                CaptureConcurrentFrame(root / "automatic-processing.png", true);
                nativeProcessingCaptured = true;
                automaticScene.SetForeground(false);
                std::cout << "Native processing capture: " << root / "automatic-processing.png" << '\n' << std::flush;
            }
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        const auto wait = [&](auto ready, const char* message) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(180);
            auto reportAt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!ready() && std::chrono::steady_clock::now() < until) {
                frame();
                if (std::chrono::steady_clock::now() >= reportAt) {
                    std::cout << "Waiting: " << message << '\n';
                    if (const auto current = automatic.Current()) {
                        std::lock_guard<std::mutex> lock(current->mutex);
                        std::cout << "Automatic " << int(current->progress.load() * 100) << "% " << current->status << '\n';
                    }
                    for (auto* editor : {a.get(), b.get()}) {
                        double progress = 0; std::string stage;
                        if (editor->GetActiveBracketingProcessingDiagnostic(progress, stage))
                            std::cout << "Manual " << int(progress * 100) << "% " << stage << '\n';
                    }
                    std::cout << std::flush;
                    reportAt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                }
            }
            Check(ready(), message);
        };
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
        Check(!candidate.identity.empty() && automatic.SetRoot(root, 0), "Automatic queue setup failed.");
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
        editing->SetCurrentProjectName("Editing during brackets");
        editing->AddReformatNodeAt({10, 20});
        editing->AddOutputNodeAt({200, 20});
        auto editSave = std::make_shared<Save>();
        Check(editing->RequestSaveProjectAs(root / "Editing during brackets", [editSave](bool ok) {
            ++editSave->calls; editSave->success = ok;
        }), "A bracket blocked another project's save.");
        wait([&] { return nativeProcessingCaptured; }, "Live automatic processing capture timed out.");
        Check(observedConcurrentWork,
            "The automatic first render did not coexist with both manual project jobs.");
        a->CancelBracketingPresentation();
        wait([&] { return !a->GetActiveBracketingProcessingDiagnostic(progress, stage) &&
            !b->GetActiveBracketingProcessingDiagnostic(progress, stage) && !automatic.HasWork() && editSave->calls; },
            "Concurrent processing or saving timed out.");
        Check(editSave->calls == 1 && editSave->success, "Independent edit save failed.");
        Check(!b->DidActiveMultiFrameProcessingForQueueFail(&error), error);
        const auto completed = automatic.ConsumeCompleted();
        Check(completed && completed->outcome == AutoBracket::State::Completed,
            completed ? completed->status : "Automatic completion was lost.");
        Check(nativeProcessingCaptured, "No native animation with real in-flight burst images was captured.");
        Check(!a->IsActiveMultiFrameProcessingForQueueBusy(), "Canceled bracket did not stop.");
        Check(a->StartBracketingProcessing(true, &error), error);
        wait([&] { return !a->GetActiveBracketingProcessingDiagnostic(progress, stage); }, "Canceled bracket could not restart.");
        Check(!a->DidActiveMultiFrameProcessingForQueueFail(&error), error);
        auto savedA = std::make_shared<Save>(), savedB = std::make_shared<Save>();
        a->ConsumeRawWorkspaceCatalogChanged();
        b->ConsumeRawWorkspaceCatalogChanged();
        Check(a->RequestSaveCurrentProject("Manual A", [savedA](bool ok) { ++savedA->calls; savedA->success = ok; }), "Save A rejected.");
        Check(b->RequestSaveCurrentProject("Manual B", [savedB](bool ok) { ++savedB->calls; savedB->success = ok; }), "Save B rejected.");
        wait([&] { return savedA->calls && savedB->calls; }, "Overlapping bracket saves timed out.");
        Check(savedA->calls == 1 && savedA->success && savedB->calls == 1 && savedB->success, "Overlapping bracket saves failed.");
        Check(a->ConsumeRawWorkspaceCatalogChanged() && b->ConsumeRawWorkspaceCatalogChanged() &&
            !a->ConsumeRawWorkspaceCatalogChanged() && !b->ConsumeRawWorkspaceCatalogChanged(),
            "Committed manual results did not publish a coalesced cross-tab catalog change.");
        const std::vector<std::filesystem::path> completedPaths{
            a->GetCurrentProjectFileName(), b->GetCurrentProjectFileName(), completed->item.projectPath};
        std::set<std::string> completedIds;
        std::vector<nlohmann::json> savedSnapshots;
        for (const auto& path : completedPaths) {
            const auto opened = Project::OpenProjectStore(path);
            Check(bool(opened), opened.message);
            completedIds.insert(opened.snapshot.projectId);
            savedSnapshots.push_back(Project::SerializeRawProjectSnapshot(opened.snapshot));
            const auto* set = Project::FindSourceSet(opened.snapshot, opened.snapshot.activeSourceSetId);
            Check(set && Project::HasSavedBracketingResult(opened.store, opened.snapshot, *set),
                "A completed project has no committed current engine output.");
            auto metadataOnly = opened.snapshot;
            auto* metadataSet = Project::FindSourceSet(metadataOnly, metadataOnly.activeSourceSetId);
            metadataSet->settings["bracketingResult"].erase("identity");
            Check(!Project::HasSavedBracketingResult(metadataOnly, *metadataSet),
                "Incomplete output metadata claimed a completed burst.");
            metadataOnly = opened.snapshot;
            ++metadataOnly.hdrInputRevision;
            metadataSet = Project::FindSourceSet(metadataOnly, metadataOnly.activeSourceSetId);
            Check(!Project::HasSavedBracketingResult(metadataOnly, *metadataSet),
                "A stale input revision claimed a completed burst.");
            Raw::Bracketing::BracketingResult restored;
            Check(Project::RestoreBracketingResult(opened.store, opened.snapshot,
                opened.snapshot.activeSourceSetId, restored, error), error);
            Check(bool(restored.raw), "Saved bracket lost its result.");
            Project::BracketingJobRequest reopen;
            reopen.store = opened.store; reopen.snapshot = opened.snapshot;
            reopen.setId = set->sourceSetId; reopen.cacheRoot = root / "reopen-cache";
            Check(Raw::Bracketing::Deserialize(set->settings.at("bracketing"), reopen.recipe, error), error);
            bool didRestore = false, enginePublished = false;
            reopen.didRestoreResult = [&] { didRestore = true; };
            reopen.reportPresentation = [&](const auto&) { enginePublished = true; };
            const auto reopened = Project::RunBracketingJob(reopen);
            Check(didRestore && !enginePublished && reopened.raw && !reopened.analysis &&
                reopened.identity == restored.identity &&
                reopened.raw->contentIdentityHash == restored.raw->contentIdentityHash,
                "Reopening recomputed the burst or changed its saved output.");
            std::cout << "Saved burst: " << path << '\n' << std::flush;
        }
        Check(completedIds.size() == 3, "Independent first renders reused a project identity.");
        AutoBracket::Queue reconciled;
        reconciled.root = root;
        const std::map<std::string, std::pair<std::string, std::filesystem::path>> represented{
            {candidate.identity, {completed->item.projectId, completed->item.projectPath}}};
        AutoBracket::Reconcile(reconciled, {candidate}, represented);
        Check(reconciled.items.size() == 1 && reconciled.items.front().state == AutoBracket::State::Completed,
            "A saved manual or automatic result did not satisfy its exact candidate.");
        AutoBracket::Reconcile(reconciled, {candidate}, {});
        Check(reconciled.items.front().state == AutoBracket::State::Attention,
            "A legacy completed queue entry without current output evidence remained complete.");
        AutoBracket::Reconcile(reconciled, {candidate}, represented);
        Check(reconciled.items.front().state == AutoBracket::State::Completed,
            "The same project's verified current result did not repair queue completion.");
        Check(a->GetProjectDocumentId() != b->GetProjectDocumentId() &&
            a->GetCurrentProjectName() == "Manual A" && b->GetCurrentProjectName() == "Manual B" &&
            editing->GetCurrentProjectName() == "Editing during brackets", "Project names or identities crossed tabs.");
        const auto completedBPath = b->GetCurrentProjectFileName();
        b->BeginBracketingDraft(true);
        const auto& subsetSources = b->GetRawWorkspaceState().sources;
        const auto subsetSource = std::find_if(subsetSources.begin(), subsetSources.end(),
            [&](const auto& source) { return source.absolutePath == sources.front(); });
        Check(subsetSource != subsetSources.end(), "The subset source is missing from its actual catalog.");
        // New bracket mirrors Gallery's current selection. Explicitly select
        // the subset instead of adding one file to the previous full selection.
        b->SelectBracketingSources({subsetSource->relativePathKey}, false, false);
        Check(b->GetRawWorkspaceState().selectedSourceKeys.size() == 1,
            "The subset action retained the previous full Gallery selection.");
        bool subsetImported = false;
        wait([&] {
            if (subsetImported) return true;
            std::string subsetError;
            subsetImported = b->CommitBracketingDraft(false, &subsetError);
            if (!subsetImported) Check(subsetError == "Wait for capture metadata inspection to finish.", subsetError);
            return subsetImported;
        }, "Subset draft metadata/import timed out.");
        wait([&] { return b->HasDeferredLoadedProjectApplyCoreFinished() &&
            !b->IsDeferredLoadedProjectApplyActive() && b->IsBracketingActive() &&
            b->GetCurrentProjectFileName() != completedBPath; }, "Subset draft activation timed out.");
        frame();
        wait([&] { return !b->IsRawWorkspaceProjectSaveBusy() && !b->IsDirty(); }, "Subset draft save timed out.");
        const auto subset = Project::OpenProjectStore(b->GetCurrentProjectFileName());
        Check(bool(subset), subset.message);
        const auto* subsetSet = Project::FindSourceSet(subset.snapshot, subset.snapshot.activeSourceSetId);
        Check(subsetSet && subsetSet->frames.size() == 1 && !completedIds.count(subset.snapshot.projectId) &&
            !Project::HasSavedBracketingResult(subset.store, subset.snapshot, *subsetSet) &&
            !b->IsActiveMultiFrameProcessingForQueueBusy(),
            "A new subset reused a completed project or processed without the user.");
        for (std::size_t index = 0; index < completedPaths.size(); ++index) {
            const auto preserved = Project::OpenProjectStore(completedPaths[index]);
            Check(preserved && Project::SerializeRawProjectSnapshot(preserved.snapshot) == savedSnapshots[index],
                "Creating a subset changed an existing completed project.");
        }
        const auto scan = RawWorkspace::ScanWorkspace(root);
        Check(scan.success && scan.sources.size() == 2, "Focused Gallery source scan failed.");
        RawWorkspace::WorkspaceState catalog;
        catalog.workspaceRoot = root; catalog.sources = scan.sources;
        Check(RawWorkspace::DiscoverSourceSetProjects(RawWorkspace::BuildManagedLayout(root),
            catalog.sources, catalog.sourceSetProjects), "Focused project discovery failed.");
        const auto cards = RawWorkspace::BuildGalleryPresentation(catalog);
        RawWorkspace::RawGallerySimilarityStack originalStack;
        for (const auto& source : catalog.sources) originalStack.sourceKeys.push_back(source.relativePathKey);
        auto originalKeys = originalStack.sourceKeys;
        std::sort(originalKeys.begin(), originalKeys.end());
        const auto matchesFull = [&](const auto& completedSets) {
            return std::any_of(completedSets.begin(), completedSets.end(),
                [&](const auto& set) { return set.sourceKeys == originalKeys; });
        };
        Check(matchesFull(RawWorkspace::BuildGalleryCompletedBurstSourceSets(catalog)),
            "A fully represented finished burst did not match its original stack.");
        // Exercise membership matching with copies of the discovered catalog.
        // This models a finished subset without changing any saved output.
        auto subsetOnly = catalog;
        for (auto& source : subsetOnly.sources) for (auto& membership : source.sourceSetProjectMemberships)
            membership.hasSavedBracketingResult = membership.projectId == subset.snapshot.projectId;
        const auto subsetCompletions = RawWorkspace::BuildGalleryCompletedBurstSourceSets(subsetOnly);
        Check(subsetCompletions.size() == 1 && subsetCompletions.front().sourceKeys.size() == 1 &&
            !matchesFull(subsetCompletions), "A completed subset falsely completed the full original stack.");
        auto fullAndSubset = catalog;
        for (auto& source : fullAndSubset.sources) for (auto& membership : source.sourceSetProjectMemberships)
            if (membership.projectId == subset.snapshot.projectId) membership.hasSavedBracketingResult = true;
        Check(matchesFull(RawWorkspace::BuildGalleryCompletedBurstSourceSets(fullAndSubset)),
            "A subset variant hid the already completed full capture set.");
        auto incomplete = catalog;
        for (auto& membership : incomplete.sources.front().sourceSetProjectMemberships)
            membership.hasSavedBracketingResult = false;
        Check(!matchesFull(RawWorkspace::BuildGalleryCompletedBurstSourceSets(incomplete)),
            "Incomplete result membership qualified as the full completed stack.");
        auto missingOriginal = catalog;
        missingOriginal.sources.pop_back();
        Check(RawWorkspace::BuildGalleryCompletedBurstSourceSets(missingOriginal).empty(),
            "A missing original shrank a larger finished burst into a smaller completed set.");
        auto brackets = cards;
        RawWorkspace::FilterGalleryProjectCards(brackets, RawWorkspace::GalleryContentMode::Bracket);
        const auto families = RawWorkspace::BuildGalleryProjectStacks(brackets, {originalStack});
        Check(brackets.projects.size() == 4 && families.size() == 1 && families.front().projectIndices.size() == 4,
            "Same-capture or subset bracket versions were not one selectable Gallery family.");
        Check(std::count_if(brackets.projects.begin(), brackets.projects.end(),
            [](const auto& project) { return project.hasSavedBracketingResult; }) == 3,
            "Gallery completion treated a pending subset as a finished burst.");
        for (const auto& group : cards.groups) for (const auto& source : group.sources)
            Check(source.hasSavedBracketingResult, "An original capture lost its completed-burst badge evidence.");
        const auto indexed = Project::ProjectIndex::Get().SnapshotForRoot(
            RawWorkspace::BuildManagedLayout(root).projectsDirectory);
        Check(std::count_if(indexed.begin(), indexed.end(), [](const auto& project) {
            return project.hasSavedBracketingResult && !project.savedBracketingSourceSetIds.empty(); }) == 3,
            "Canonical index completion differs from saved engine output.");
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({1440, 900});
        ImGui::Begin("Project pill appearance", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        const auto palette = Header::ResolvePalette(appearance.GetClearColor());
        auto* draw = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos({32, 28}); ImGui::TextUnformatted("Project pills: normal and automatic");
        for (unsigned index = 0; index < 4; ++index) {
            const float y = 80.f + index * 44.f;
            const bool selected = index % 2 == 0, background = index >= 2;
            Header::DrawProjectPill(draw, {32, y}, {112, y + 14}, 1.f, palette,
                {selected, false, false, false, background});
            draw->AddText({140, y - 1}, ImGui::GetColorU32(ImGuiCol_Text),
                background ? (selected ? "Automatic - active" : "Automatic - inactive") :
                    (selected ? "Normal - active" : "Normal - inactive"));
        }
        ImGui::End(); ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 1440, 900);
        const auto previewBackground = appearance.GetClearColor();
        glClearColor(previewBackground.x, previewBackground.y, previewBackground.z, previewBackground.w);
        glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        CaptureConcurrentFrame(root / "project-pill-style.png");
        std::cout << "PASS two manual brackets plus an automatic bracket, independent cancellation/restart, editing, overlapping saves, "
            "saved-output reopen without processing, independent pending subset and completed Gallery family\n";
        std::cout << "Validation projects: " << root << '\n';
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "Concurrent bracket validation failed: " << error.what() << '\n';
    }
    automatic.Shutdown();
    automaticScene.SetForeground(false);
    for (auto* editor : {a.get(), b.get(), editing.get()}) editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();
    editing.reset(); b.reset(); a.reset();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return success;
}
}
