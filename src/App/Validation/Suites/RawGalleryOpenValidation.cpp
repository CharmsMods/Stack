#include "App/Validation/ValidationSuites.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Renderer/GLLoader.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Renderer/WorkspaceCompositor.h"
#include "App/WorkspaceSwitcher.h"
#include <backends/imgui_impl_opengl3.h>
#include <functional>
#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Stack::Validation {
void ValidateRawNodeTransitions(EditorModule&, const std::function<void(const std::function<void()>&)>&);

bool ValidateRawGalleryOpen(const std::string& rawPath, const std::string& scratchPath, bool persistenceChecks,
    bool nodeTransitionChecks) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(1000, 700, "RAW Gallery opening diagnostic", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {1000, 700};
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0, fontHeight = 0;
    if (nodeTransitionChecks) ImGui_ImplOpenGL3_Init("#version 430");
    else ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    Async::TaskSystem::Get().Initialize();
    auto editor = std::make_unique<EditorModule>();
    bool success = false;
    try {
        // Scan only an isolated copy: Gallery discovery writes its own catalog.
        const auto root = std::filesystem::absolute(std::filesystem::u8path(scratchPath)) /
            Project::GenerateStableUuid();
        std::filesystem::create_directories(root);
        const auto input = std::filesystem::absolute(std::filesystem::u8path(rawPath));
        std::filesystem::copy_file(input, root / input.filename());
        std::cout << "Diagnostic directory: " << root << '\n' << std::flush;
        editor->SetDocumentPersistenceEnabled(false);
        editor->SetWorkspaceAppStatePersistenceEnabled(false);
        editor->Initialize(window, nullptr, false);
        editor->SetPermanentGalleryWorkspace(false);
        editor->EnterRawWorkspaceRootTab();
        editor->SetRawWorkspaceFolder(root);

        Stack::Renderer::WorkspaceCompositor compositor;
        Stack::Workspace::Switcher switcher;
        const auto frame = [&](const std::function<void()>& action = {}) {
            glfwPollEvents();
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            if (nodeTransitionChecks) ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            editor->PumpNonRenderingWork(2.5);
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({1000, 700});
            ImGui::Begin("RAW diagnostic", nullptr,
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
            if (nodeTransitionChecks) {
                editor->UpdateNodeOpenTransition();
                const auto origin = ImGui::GetCursorScreenPos();
                const auto size = ImGui::GetContentRegionAvail();
                editor->SetGraphReturnBounds(origin, {origin.x+size.x, origin.y+size.y});
                if (action) action();
                editor->RenderUI();
            } else editor->RenderRawWorkspaceLabUI();
            ImGui::End();
            if (nodeTransitionChecks) {
                ImGui::Render();
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0,0,1000,700);
                glClearColor(.04f,.04f,.04f,1); glClear(GL_COLOR_BUFFER_BIT);
                compositor.BeginFrame();
                compositor.SetRawImageBackdrop(editor->GetRawImageBackdrop());
                Stack::Renderer::WorkspaceCompositor::ProjectFrame project;
                project.rootView=1; project.bodyMin={0,0}; project.bodyMax={1000,700};
                if (!compositor.Render(ImGui::GetDrawData(),switcher,{.04f,.04f,.04f,1},nullptr,-1,1,false,&project))
                    throw std::runtime_error("Node transition compositor failed");
            } else ImGui::EndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        };
        const auto report = [&] {
            std::cout << "scan=" << editor->IsRawWorkspaceScanBusy()
                << " sources=" << editor->GetRawWorkspaceState().sources.size()
                << " load=" << editor->IsRawWorkspaceProjectLoadBusy()
                << " deferred=" << editor->IsDeferredLoadedProjectApplyActive()
                << " active=" << editor->IsRawWorkspaceProjectActive()
                << " connected=" << editor->GetNodeGraph().IsOutputConnected()
                << " rendering=" << editor->IsEditorRenderBusy()
                << " generation=" << editor->GetViewportOutputRenderGeneration()
                << " status=" << editor->GetRawWorkspaceProjectLoadStatusText()
                << " presentation=" << editor->GetActiveRawWorkspacePresentationDiagnosticForValidation()
                << '\n' << std::flush;
        };
        const auto wait = [&](auto ready, const char* failure) {
            const auto begin = std::chrono::steady_clock::now();
            auto nextReport = begin;
            while (!ready() && std::chrono::steady_clock::now() - begin < std::chrono::seconds(60)) {
                frame();
                if (std::chrono::steady_clock::now() >= nextReport) {
                    report();
                    nextReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                }
            }
            report();
            if (!ready()) throw std::runtime_error(failure);
        };
        wait([&] { return !editor->IsRawWorkspaceScanBusy() &&
            !editor->GetRawWorkspaceState().sources.empty(); }, "Gallery scan timed out");
        const auto key = editor->GetRawWorkspaceState().sources.front().relativePathKey;
        // This is the same public action used by the shell after a Gallery open
        // creates a project tab and that tab's source catalog is ready.
        editor->SelectRawWorkspaceSourceForGallery(key, false, false, true);
        wait([&] { return !editor->IsRawWorkspaceProjectLoadBusy() &&
            editor->HasActiveRawWorkspacePresentationForValidation(); }, "Gallery first-frame timed out");
        if (editor->HasDeferredLoadedProjectApplyFailed())
            throw std::runtime_error(editor->GetDeferredLoadedProjectStatusText());
        unsigned texture = 0;
        int width = 0, height = 0;
        if (!editor->TryGetActiveRawWorkspacePresentationTexture(texture, width, height))
            throw std::runtime_error("First-frame texture is unavailable");
        std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4);
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        float low = 1e30f, high = -1e30f;
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            if (i % 4 == 3) continue;
            if (!std::isfinite(pixels[i])) throw std::runtime_error("Nonfinite first-frame pixel");
            low = std::min(low, pixels[i]);
            high = std::max(high, pixels[i]);
        }
        if (high - low < 0.001f) throw std::runtime_error("First-frame texture is blank");
        std::cout << "PASS Gallery RAW first frame: " << width << 'x' << height
            << " RGB range=" << low << ".." << high << '\n' << std::flush;
        if (nodeTransitionChecks) {
            if (const auto* projectPath = std::getenv("STACK_NODE_TRANSITION_PROJECT")) {
                // Load a read-only candidate; persistence stays disabled throughout this fixture.
                auto loaded = Project::ProjectOpenCoordinator::Load(std::filesystem::u8path(projectPath));
                if (!loaded || !editor->BeginDeferredLoadedProjectApply(loaded.candidate))
                    throw std::runtime_error("Could not load the transition diagnostic project");
                wait([&] { return !editor->IsDeferredLoadedProjectApplyActive() &&
                    editor->HasActiveRawWorkspacePresentationForValidation(); }, "Diagnostic project open timed out");
                if (editor->HasDeferredLoadedProjectApplyFailed())
                    throw std::runtime_error(editor->GetDeferredLoadedProjectStatusText());
                unsigned loadedTexture=0; int loadedWidth=0,loadedHeight=0;
                if (!editor->TryGetActiveRawWorkspacePresentationTexture(loadedTexture,loadedWidth,loadedHeight) ||
                    std::max(loadedWidth,loadedHeight)<256)
                    throw std::runtime_error("Opening the diagnostic project published a placeholder-sized photo");
            }
            ValidateRawNodeTransitions(*editor, frame);
        }
        if (persistenceChecks) {
            const auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
            require(!editor->IsUnifiedProjectStoreActive(), "Browsing unexpectedly created a project store");
            editor->AddReformatNodeAt({600, 100});
            const auto capturedNodes = editor->GetNodeGraph().GetNodes().size();
            const auto id = editor->EnsureProjectDocumentId();
            bool completed = false, saved = false;
            editor->SetDocumentPersistenceEnabled(true);
            require(editor->RequestSaveCurrentProject({}, [&](bool ok) { saved = ok; completed = true; }), "First RAW save was rejected");
            editor->AddReformatNodeAt({700, 220});
            editor->SetDocumentPersistenceEnabled(false);
            wait([&] { return completed; }, "First RAW save timed out");
            require(saved && editor->IsUnifiedProjectStoreActive(), "First RAW save did not create a managed store");
            require(editor->GetProjectDocumentId() == id && editor->IsDirty(), "First save changed identity or cleared a later edit");
            auto store = editor->GetProjectSession().store;
            const auto first = Project::OpenProjectStore(store->StoragePath());
            require(first && first.snapshot.pipelineData.at("nodeGraph").at("nodes").size() == capturedNodes,
                "First RAW save did not preserve the captured graph");
            require(editor->GetNodeGraph().GetNodes().size() == capturedNodes + 1, "First-save completion replaced the live graph");
            completed = false; saved = false;
            editor->SetDocumentPersistenceEnabled(true);
            require(editor->RequestSaveCurrentProject({}, [&](bool ok) { saved = ok; completed = true; }), "Second RAW save was rejected");
            editor->SetDocumentPersistenceEnabled(false);
            wait([&] { return completed; }, "Second RAW save timed out");
            require(saved && !editor->IsDirty(), "Second save did not commit the remaining edits");
            std::cout << "PASS first RAW save captures the graph, keeps identity, and preserves later edits\n";

            editor->SetDocumentPersistenceEnabled(true);
            completed = false; saved = false;
            const auto versionPath = root / "Closet" / "Projects" / "Live version";
            require(editor->RequestProjectCopy(versionPath, false, false, [&](bool ok) { saved = ok; completed = true; }), "Version request was rejected");
            wait([&] { return completed; }, "Version creation timed out");
            require(saved && editor->GetProjectDocumentId() == id, "Version creation changed its source project");
            const auto version = Project::OpenProjectStore(versionPath);
            require(version && version.snapshot.projectId != id, "Version reused the original document identity");
            const auto assetId = first.snapshot.rawWorkspaceData.at("managedAssetId").get<std::string>();
            require(version.store->ResolveAssetFile(assetId) == store->ResolveAssetFile(assetId), "Version duplicated its source asset");
            completed = false; saved = false;
            const auto portable = root / "Complete.stack";
            require(editor->RequestProjectCopy(portable, true, false, [&](bool ok) { saved = ok; completed = true; }), "Export request was rejected");
            wait([&] { return completed; }, "Portable export timed out");
            require(saved, "Portable export failed");
            std::filesystem::rename(root / input.filename(), root / "original-renamed.raw");
            auto reopened = Project::ProjectOpenCoordinator::Load(store->StoragePath());
            require(static_cast<bool>(reopened), "Managed RAW project failed to reopen after the original was renamed");
            auto portableReopened = Project::ProjectOpenCoordinator::Load(portable);
            require(static_cast<bool>(portableReopened), "Self-contained RAW export failed to reopen");
            require(portableReopened.candidate->pipelineData.at("nodeGraph").at("nodes").size() == capturedNodes + 1,
                "Export omitted authored graph edits");
            std::cout << "PASS RAW version shares assets; working and portable projects reopen after original rename\n";
            completed = false; saved = false;
            const auto copyWithLaterEdits = root / "Closet" / "Projects" / "Copy with later edits";
            require(editor->RequestSaveProjectAs(copyWithLaterEdits, [&](bool ok) { saved = ok; completed = true; }), "Save As was rejected");
            editor->AddReformatNodeAt({800, 330});
            wait([&] { return completed; }, "Save As with later edits timed out");
            require(saved && editor->GetProjectDocumentId() == id && editor->IsDirty(),
                "Save As lost later edits or attached them to an older asset store");
            const auto detachedCopy = Project::OpenProjectStore(copyWithLaterEdits);
            require(detachedCopy && detachedCopy.snapshot.pipelineData.at("nodeGraph").at("nodes").size() == capturedNodes + 1,
                "Save As did not preserve its captured revision");
            completed = false; saved = false;
            const auto activatedCopy = root / "Closet" / "Projects" / "Active copy";
            require(editor->RequestSaveProjectAs(activatedCopy, [&](bool ok) { saved = ok; completed = true; }), "Clean Save As was rejected");
            wait([&] { return completed; }, "Clean Save As timed out");
            require(saved && editor->GetProjectDocumentId() != id && !editor->IsDirty() &&
                editor->GetProjectSession().store->StoragePath() == activatedCopy,
                "Save As did not activate its independent saved baseline");
            std::cout << "PASS Save As preserves edits made during copying and activates an unchanged captured document\n";

            editor->SetRawWorkspaceFolder({});
            require(editor->BeginDeferredLoadedProjectApply(portableReopened.candidate), "Portable document apply was rejected");
            wait([&] { return !editor->IsDeferredLoadedProjectApplyActive(); }, "Portable document apply timed out");
            require(!editor->HasDeferredLoadedProjectApplyFailed(), "Portable document apply failed");
            editor->AddReformatNodeAt({900, 400});
            completed = false; saved = false;
            require(editor->RequestSaveCurrentProject({}, [&](bool ok) { saved = ok; completed = true; }), "Portable adoption save was rejected");
            wait([&] { return completed; }, "Portable adoption save timed out");
            require(saved && editor->GetProjectSession().store->StoragePath().parent_path() == root / "Closet" / "Projects",
                "Imported RAW did not adopt into Closet without a Gallery folder");
            const auto unchangedExport = Project::OpenProjectStore(portable);
            require(unchangedExport && unchangedExport.snapshot.pipelineData.at("nodeGraph").at("nodes").size() == capturedNodes + 1,
                "Adoption overwrote the portable export");
            std::cout << "PASS portable RAW adopts into Closet and preserves the exported file\n";

            const auto bracketPath = root / "Closet" / "Projects" / "Bracket draft";
            std::string bracketError;
            require(editor->CreateMultiFrameRawProject(bracketPath, Project::ProjectStorageKind::DirectoryBundle,
                "Bracket draft", "Captures", Project::MultiFrameOperationIntent::RawCaptureSet, {input}, 0, &bracketError),
                bracketError.empty() ? "Bracket creation failed" : bracketError.c_str());
            wait([&] { return !editor->IsDeferredLoadedProjectApplyActive(); }, "Bracket open timed out");
            require(editor->IsBracketingActive(), "Bracket project was not activated");
            editor->BeginBracketingDraft();
            const auto& bracketSnapshot = *editor->GetProjectSession().snapshot;
            const auto setId = bracketSnapshot.activeSourceSetId;
            const auto inputRevision = bracketSnapshot.hdrInputRevision;
            const auto* sourceSet = Project::FindSourceSet(bracketSnapshot, setId);
            require(sourceSet && !sourceSet->frames.empty(), "Bracket lost its source frames");
            editor->RemoveBracketingDraftSource(sourceSet->frames.front().frameId);
            completed = false; saved = false;
            require(editor->RequestSaveCurrentProject({}, [&](bool ok) { saved = ok; completed = true; }), "Bracket draft save was rejected");
            wait([&] { return completed; }, "Bracket draft save timed out");
            const auto bracketSaved = Project::OpenProjectStore(bracketPath);
            require(saved && bracketSaved && !editor->IsDirty(), "Bracket draft was not saved");
            const auto* storedSet = Project::FindSourceSet(bracketSaved.snapshot, setId);
            require(storedSet && storedSet->settings.contains("bracketingDraft") &&
                storedSet->settings.at("bracketingDraftSelection").empty() &&
                bracketSaved.snapshot.hdrInputRevision == inputRevision,
                "Saving a bracket draft lost its selection or promoted it to processed inputs");
            require(bracketSaved.store->ResolveAssetFile(assetId) == store->ResolveAssetFile(assetId),
                "Bracket did not share the existing managed source");
            std::cout << "PASS bracket draft saves in the background, retains pending inputs, and shares its RAW\n";
        }
        compositor.Shutdown();
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "RAW Gallery diagnostic failed: " << error.what() << '\n' << std::flush;
    }
    editor->RequestWorkerShutdownForAppClose();
    editor->Shutdown();
    Async::TaskSystem::Get().Shutdown();
    editor.reset();
    if (nodeTransitionChecks) ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return success;
}

} // namespace Stack::Validation
