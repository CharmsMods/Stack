#include "App/Validation/ValidationSuites.h"
#include "App/WorkspaceInputScope.h"
#include "App/WorkspaceSwitcher.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/EditorModule.h"
#include "Persistence/BracketingProject.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Renderer/GLLoader.h"
#include "Renderer/WorkspaceCompositor.h"
#include "ThirdParty/stb_image_write.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {
void RequireBracketSurface(bool value, const std::string& error) {
    if (!value) throw std::runtime_error(error);
}

void CaptureBracketSurface(const std::filesystem::path& path) {
    constexpr int width = 1440, height = 900;
    std::vector<unsigned char> bytes(std::size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    for (int y = 0; y < height / 2; ++y)
        for (int x = 0; x < width * 4; ++x)
            std::swap(bytes[std::size_t(y) * width * 4 + x],
                bytes[std::size_t(height - y - 1) * width * 4 + x]);
    RequireBracketSurface(stbi_write_png(path.string().c_str(), width, height, 4,
        bytes.data(), width * 4) != 0, "Could not save native Bracketing capture.");
}

void CheckBracketSource(const EditorNodeGraph::Graph& graph) {
    unsigned sources = 0;
    for (const auto& node : graph.GetNodes()) {
        RequireBracketSurface(node.kind != EditorNodeGraph::NodeKind::RawProjectFrame,
            "Bracket graph still contains a per-capture provenance node.");
        if (node.kind != EditorNodeGraph::NodeKind::MultiFrameHdr) continue;
        ++sources;
        RequireBracketSurface(node.multiFrameHdr.bracketingSource,
            "Bracket source does not use its source-node presentation.");
        RequireBracketSurface(!graph.FindInputLink(node.id,
            EditorNodeGraph::MfdFrameInputSocketId(node.multiFrameHdr.frameBindings.empty()
                ? std::string{} : node.multiFrameHdr.frameBindings.front().frameId)),
            "Bracket source still has capture-input wiring.");
    }
    RequireBracketSurface(sources == 1, "Expected one Bracketing source node.");
}
}

// One real import/process/save/reopen lifecycle, or --draft-only to exercise
// Gallery's pre-import capture-set entry. Both use production View/Graph/panel
// drawing. All project/catalog/capture writes remain under the scratch root.
bool ValidateBracketingNodeSurface(int argc, char** argv) {
    if (argc < 3 || !glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(1440, 900, "Bracketing node surface validation", nullptr, nullptr);
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
    auto owner = std::make_unique<EditorModule>();
    auto other = std::make_unique<EditorModule>();
    bool success = false;
    {
        Renderer::WorkspaceCompositor compositor;
        Workspace::Switcher switcher;
        try {
            const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[0]));
            std::filesystem::create_directories(root);
            const bool reopenOnly = argc == 3 && std::string(argv[1]) == "--reopen";
            const bool galleryOpen = argc == 3 && std::string(argv[1]) == "--gallery-open";
            const bool catalogOnly = argc == 3 && std::string(argv[1]) == "--catalog-only";
            const bool folderFlow = argc == 4 && std::string(argv[1]) == "--folder-flow";
            const bool draftOnly = argc >= 4 && std::string(argv[1]) == "--draft-only";
            if (catalogOnly || folderFlow) for(int folder=2;folder<(folderFlow?4:3);++folder) {
                const auto workflowRoot = std::filesystem::absolute(std::filesystem::u8path(argv[folder]));
                const auto scan = RawWorkspace::ScanWorkspace(workflowRoot);
                RequireBracketSurface(scan.success && scan.sources.size() >= 2,
                    "The native Gallery check needs the actual saved workflow captures.");
                RawWorkspace::RawGalleryManualGrouping grouping;
                std::vector<std::string> members;
                for (const auto& source : scan.sources) members.push_back(source.relativePathKey);
                RequireBracketSurface(RawWorkspace::MergeRawGalleryManualStackMembers(grouping,
                    members, {members.front()}), "Could not group the workflow captures for the native stack check.");
                std::string error;
                RequireBracketSurface(RawWorkspace::SaveGalleryState(
                    RawWorkspace::BuildManagedLayout(workflowRoot), grouping, &error), error);
            }
            std::vector<std::filesystem::path> files;
            for (int index = draftOnly ? 2 : 1; !reopenOnly && !galleryOpen && !catalogOnly && !folderFlow && index < argc; ++index) {
                auto file = std::filesystem::absolute(std::filesystem::u8path(argv[index]));
                RequireBracketSurface(std::filesystem::is_regular_file(file), "Missing original RAW: " + file.string());
                if (draftOnly) {
                    const auto copies = root / "draft-originals";
                    std::filesystem::create_directories(copies);
                    const auto copy = copies / file.filename();
                    RequireBracketSurface(std::none_of(files.begin(), files.end(), [&](const auto& prior) {
                        return prior.filename() == copy.filename();
                    }), "Draft entry inputs must have distinct filenames.");
                    std::filesystem::copy_file(file, copy, std::filesystem::copy_options::overwrite_existing);
                    file = copy;
                }
                files.push_back(std::move(file));
            }
            for (auto* editor : {owner.get(), other.get()}) {
                editor->SetWorkspaceAppStatePersistenceEnabled(false);
                if (galleryOpen) editor->SetDocumentPersistenceEnabled(false);
                editor->Initialize(window, &appearance, false);
                editor->SetPermanentGalleryWorkspace(false);
                editor->SetRawWorkspaceFolder(catalogOnly || folderFlow ? std::filesystem::absolute(std::filesystem::u8path(argv[2])) : root);
                editor->EnterRawWorkspaceRootTab();
            }
            other->SetCurrentProjectName("Independent project");
            other->AddReformatNodeAt({40, 40});
            const auto otherId = other->GetProjectDocumentId();
            const auto otherPipeline = other->SerializePipeline();
            EditorModule* foreground = owner.get();
            std::function<void()> frameAction;
            std::filesystem::path textCapture;
            std::string capturedText;
            bool viewRequestObserved = false, panelRequestObserved = false;
            unsigned frames = 0;
            const auto sharedFolder=std::make_shared<std::filesystem::path>(owner->GetRawWorkspaceState().workspaceRoot);
            if(folderFlow)for(auto* editor:{owner.get(),other.get()})
                editor->SetRawWorkspaceFolderChangedHandler([sharedFolder](const auto& folder){*sharedFolder=folder;});
            const auto frame = [&] {
                glfwPollEvents();
                Async::TaskSystem::Get().PumpMainThreadTasks(8, 4);
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                if(folderFlow)for(auto* editor:{owner.get(),other.get()})editor->SetRawWorkspaceFolder(*sharedFolder);
                for (auto* editor : {owner.get(), other.get()}) {
                    const bool visible = editor == foreground;
                    editor->PumpNonRenderingWork(3, visible);
                    editor->TickBracketing(visible);
                    editor->UpdateBracketingPresentation(visible);
                }
                foreground->UpdateNodeOpenTransition();
                // The shell consumes these requests before drawing the new
                // project tab, even while its saved document is decoding.
                if (galleryOpen && foreground->ConsumeOpenMultiFrameTabRequest())
                    foreground->OpenBracketingTool();
                if (galleryOpen && foreground->ConsumeOpenEditorTabRequest())
                    foreground->SetWorkspaceMode(Project::WorkspaceMode::Graph);
                if(foreground->ConsumeOpenRawLabTabRequest()) {
                    viewRequestObserved = true;
                    foreground->SetWorkspaceMode(Project::WorkspaceMode::View);
                }
                if (draftOnly && foreground->ConsumeRawSettingsPanelRequest()) {
                    panelRequestObserved = true;
                    foreground->SetWorkspacePanelOpen(true);
                }
                const float panelWidth = (!draftOnly || foreground->WorkspacePanelOpen()) ? 320.f : 0.f;
                // AppShell draws its hosted navigator before the body. That
                // marks this frame hosted and suppresses the inline fallback.
                if(catalogOnly || folderFlow)foreground->RenderRawWorkspaceSectionPanel({0, 0}, {320, 900}, panelWidth);
                ImGui::SetNextWindowPos({panelWidth, 0});
                ImGui::SetNextWindowSize({1440.f - panelWidth, 900});
                ImGui::Begin("Bracket native workspace", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
                const auto logPath = std::exchange(textCapture, std::filesystem::path{});
                if (!logPath.empty()) {
                    std::ofstream clearLog(logPath, std::ios::trunc);
                    RequireBracketSurface(bool(clearLog), "Could not create native UI text capture.");
                    clearLog.close();
                    ImGui::LogToFile(0, logPath.string().c_str());
                }
                foreground->SetGraphReturnBounds({panelWidth, 0}, {1440, 900});
                if(frameAction) {
                    auto action=std::exchange(frameAction,{});
                    action();
                }
                {
                    Workspace::InputScope locked(foreground->IsBracketingPresentationActive(), false);
                    if(catalogOnly || folderFlow)foreground->RenderRawWorkspaceLabUI();else foreground->RenderUI();
                }
                ImGui::End();
                // End() finishes logging for the workspace root. The Source
                // panel is a sibling window, so append its labels separately.
                if (!logPath.empty()) ImGui::LogToFile(0, logPath.string().c_str());
                if(!catalogOnly && !folderFlow)foreground->RenderRawWorkspaceSectionPanel({0, 0}, {320, 900}, panelWidth);
                foreground->RenderBracketingPresentation(appearance.GetClearColor(), {0, 0}, {1440, 900});
                if (!logPath.empty()) {
                    ImGui::LogFinish();
                    std::ifstream log(logPath);
                    RequireBracketSurface(bool(log), "Could not read native UI text capture.");
                    capturedText.assign(std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>());
                }
                ImGui::Render();
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, 1440, 900);
                const auto clear = appearance.GetClearColor();
                glClearColor(clear.x, clear.y, clear.z, clear.w);
                glClear(GL_COLOR_BUFFER_BIT);
                compositor.BeginFrame();
                compositor.SetRawImageBackdrop(foreground->GetRawImageBackdrop());
                Renderer::WorkspaceCompositor::ProjectFrame project;
                project.rootView = foreground->IsViewMode() ? 1 : 0;
                project.bodyMin = {0, 0}; project.bodyMax = {1440, 900};
                RequireBracketSurface(compositor.Render(ImGui::GetDrawData(), switcher, clear,
                    nullptr, -1, foreground == owner.get() ? 1 : 2, false, &project),
                    "Native bracket workspace compositor failed.");
                ++frames;
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            };
            const auto wait = [&](auto ready, const char* message, int seconds = 120) {
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
                auto reportAt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (!ready() && std::chrono::steady_clock::now() < until) {
                    frame();
                    if (std::chrono::steady_clock::now() >= reportAt) {
                        double progress = 0; std::string stage;
                        if (owner->GetActiveBracketingProcessingDiagnostic(progress, stage))
                            std::cout << "Bracket progress " << int(progress * 100) << "% " << stage << '\n' << std::flush;
                        reportAt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                    }
                }
                RequireBracketSurface(ready(), message);
            };
            if (galleryOpen) {
                const auto path = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
                const auto expected = Project::ProjectOpenCoordinator::Load(path);
                RequireBracketSurface(bool(expected), expected.error);
                RequireBracketSurface(owner->RequestOpenRawWorkspaceProjectFromGallery(path),
                    "Gallery rejected the saved-project open action.");
                wait([&] {
                    RequireBracketSurface(owner->GetProjectLoadTaskState() != Async::TaskState::Failed &&
                        !owner->HasDeferredLoadedProjectApplyFailed(), owner->GetProjectLoadStatusText());
                    return !Async::IsBusy(owner->GetProjectLoadTaskState()) &&
                        !owner->IsDeferredLoadedProjectApplyActive();
                }, "Gallery saved-project load did not finish.", 30);
                frame();
                CaptureBracketSurface(root / "gallery-open-result.png");
                RequireBracketSurface(owner->GetProjectDocumentId() == expected.candidate->projectId &&
                    !owner->GetNodeGraph().GetNodes().empty(),
                    "Gallery left an empty/wrong document after opening the saved project: " +
                        owner->GetProjectLoadStatusText());
                const bool bracket = owner->IsMultiFrameRawProjectActive();
                if (bracket) CheckBracketSource(owner->GetNodeGraph());
                owner->SetWorkspaceMode(Project::WorkspaceMode::View);
                wait([&] { return owner->HasActiveRawWorkspacePresentationForValidation(); },
                    "The saved Gallery project did not publish its View.", 30);
                unsigned texture = 0; int width = 0, height = 0;
                RequireBracketSurface(owner->TryGetActiveRawWorkspacePresentationTexture(texture, width, height),
                    "The saved Gallery project has no accepted image texture.");
                std::vector<float> pixels(std::size_t(width) * height * 4);
                glBindTexture(GL_TEXTURE_2D, texture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
                float low = pixels.front(), high = low;
                for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
                    RequireBracketSurface(std::isfinite(pixels[pixel]), "Saved Gallery View contains non-finite pixels.");
                    if (pixel % 4 != 3) { low = std::min(low, pixels[pixel]); high = std::max(high, pixels[pixel]); }
                }
                RequireBracketSurface(high - low > .001f, "Saved Gallery View is blank.");
                for (unsigned settle = 0; settle < 24; ++settle) frame();
                CaptureBracketSurface(root / "gallery-open-view.png");
                owner->SetWorkspaceMode(Project::WorkspaceMode::Graph);
                for (unsigned settle = 0; settle < 24; ++settle) frame();
                CaptureBracketSurface(root / "gallery-open-graph.png");
                RequireBracketSurface(other->GetProjectDocumentId() == otherId &&
                    other->SerializePipeline() == otherPipeline,
                    "Gallery project opening changed the independent project.");
                std::cout << "PASS Gallery saved " << (bracket ? "bracket" : "single-image")
                    << " open, exact document identity, nonempty Graph, finite nonblank View, independent project; no Process or save\n"
                    << "Captures: " << root << '\n' << std::flush;
                success = true;
            } else if (folderFlow) {
                owner->SetPermanentGalleryWorkspace(true);
                owner->OpenRawWorkspaceGalleryWorkspace();
                frame();
                // Replace a folder while its scan/preview callbacks may still
                // be queued, then repeat the shell's shared-folder propagation.
                const auto chosen=std::filesystem::absolute(std::filesystem::u8path(argv[3])).lexically_normal();
                owner->SetRawWorkspaceFolder(chosen);
                wait([&]{return !owner->IsRawWorkspaceScanBusy() && !other->IsRawWorkspaceScanBusy() &&
                    owner->GetRawWorkspaceState().sources.size()==2 && other->GetRawWorkspaceState().sources.size()==2;},
                    "Switching source folders did not publish the latest two captures.",60);
                RequireBracketSurface(*sharedFolder==chosen && owner->GetRawWorkspaceState().workspaceRoot==chosen &&
                    other->GetRawWorkspaceState().workspaceRoot==chosen,"An older folder callback replaced the user's folder.");
                for(const auto& source:owner->GetRawWorkspaceState().sources)
                    RequireBracketSurface(source.absolutePath.parent_path()==chosen,"An older folder's captures survived replacement.");
                wait([&]{return !owner->IsRawWorkspaceThumbnailBusy() && !other->IsRawWorkspaceThumbnailBusy();},
                    "The selected folder's thumbnails did not finish.",60);
                bool requested=false;
                wait([&]{
                    frameAction=[&]{owner->TickAutoBracketing(false);requested=owner->ConsumeAutoBracketWorkspaceRequest();};
                    return requested;
                },"The discovered stack did not request automatic processing without Run now.",30);
                for(unsigned settle=0;settle<30;++settle)frame();
                RequireBracketSurface(owner->GetRawWorkspaceGalleryPresentation().totalSources==2 &&
                    other->GetProjectDocumentId()==otherId && other->SerializePipeline()==otherPipeline,
                    "Folder discovery hid the captures or changed an independent project.");
                CaptureBracketSurface(root/"folder-switch.png");
                std::cout << "PASS source-folder replacement, shared propagation, actual previews/grouping, automatic workspace request without Run now; no Process\n" << std::flush;
                success=true;
            } else if (catalogOnly) {
                owner->SetPermanentGalleryWorkspace(true);
                owner->OpenRawWorkspaceGalleryWorkspace();
                wait([&] { return !owner->IsRawWorkspaceScanBusy() &&
                    !owner->GetRawWorkspaceState().sources.empty(); }, "Saved-result Gallery catalog scan timed out.");
                wait([&] { return !owner->IsRawWorkspaceThumbnailBusy(); }, "Gallery thumbnail preparation timed out.");
                const auto galleryCaptureAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
                while(std::chrono::steady_clock::now()<galleryCaptureAt)frame();
                CaptureBracketSurface(root / "gallery-completed-stacks.png");
                const auto& presentation=owner->GetRawWorkspaceGalleryPresentation();
                RequireBracketSurface(!presentation.completedBurstSourceSets.empty(),
                    "Native Gallery source memberships lost verified completion after warm scan publication.");
                RequireBracketSurface(std::any_of(presentation.projects.begin(),presentation.projects.end(),
                    [](const auto& project){return project.hasSavedBracketingResult;}),
                    "Gallery discovery lost committed burst outputs.");
                std::cout << "PASS actual native Gallery Originals with saved burst discovery; no Process\nCapture: "
                    << root / "gallery-completed-stacks.png" << '\n' << std::flush;
                success=true;
            } else if (draftOnly) {
                RequireBracketSurface(files.size() >= 2, "Draft entry needs at least two original captures.");
                owner->SetWorkspaceMode(Project::WorkspaceMode::View);
                textCapture = root / "empty-view-labels.txt";
                frame();
                RequireBracketSurface(capturedText.find("Drop an image into the graph or press Tab to start.") != std::string::npos,
                    "The ordinary empty View baseline was not rendered.");
                CaptureBracketSurface(root / "empty-view.png");
                RequireBracketSurface(other->GetProjectDocumentId() == otherId && other->SerializePipeline() == otherPipeline,
                    "Opening an empty View changed an independent project.");

                // The Gallery only scans isolated input copies. Use its public
                // selection/action APIs, then the shell's exact owner handoff.
                other->SetPermanentGalleryWorkspace(true);
                other->OpenRawWorkspaceGalleryWorkspace();
                wait([&] { return !other->IsRawWorkspaceScanBusy() &&
                    other->GetRawWorkspaceState().sources.size() == files.size(); }, "Draft Gallery scan timed out.", 60);
                bool first = true;
                for (const auto& file : files) {
                    const auto& sources = other->GetRawWorkspaceState().sources;
                    const auto found = std::find_if(sources.begin(), sources.end(), [&](const auto& source) {
                        return source.absolutePath.lexically_normal() == file.lexically_normal();
                    });
                    RequireBracketSurface(found != sources.end(), "Copied RAW is missing from Gallery.");
                    other->SelectRawWorkspaceSourceForGallery(found->relativePathKey, !first, false, false);
                    first = false;
                }
                RequireBracketSurface(other->GetRawWorkspaceState().selectedSourceKeys.size() == files.size(),
                    "Gallery did not retain all selected originals.");
                bool handedOff = false;
                other->SetRawGalleryOpenSelectionHandler([&](EditorModule::RawGalleryOpenSelection selection, bool bracket) {
                    RequireBracketSurface(bracket && selection.bracketSources.size() == files.size(),
                        "Gallery did not hand off the selected capture set.");
                    for (const auto& file : files) RequireBracketSurface(std::find(selection.bracketSources.begin(),
                        selection.bracketSources.end(), file) != selection.bracketSources.end(),
                        "Gallery changed a selected original path.");
                    owner->BeginBracketingDraft(true);
                    owner->AddBracketingDraftFiles(selection.bracketSources);
                    owner->OpenBracketingTool();
                    RequireBracketSurface(owner->WorkspacePanelOpen(),
                        "Capture-set entry did not open Source by default.");
                    owner->RequestOpenRawLabTab();
                    handedOff = true;
                });
                frameAction = [&] {
                    // On the permanent Gallery this public command executes
                    // CreateCaptureSet with its captured Gallery selection.
                    other->OpenBracketingTool();
                    RequireBracketSurface(handedOff, "Gallery Create Capture Set was rejected.");
                };
                viewRequestObserved = panelRequestObserved = false;
                frame();
                frame(); // Consume the operation's View/panel requests and start metadata inspection.
                other->SetRawGalleryOpenSelectionHandler({});
                wait([&] { return owner->ProjectTasks().IsIdle() && !owner->IsRawWorkspaceScanBusy(); },
                    "Draft capture metadata inspection timed out.", 60);
                frame(); // Adopt completed metadata without importing or processing.
                textCapture = root / "draft-view-labels.txt";
                frame();
                CaptureBracketSurface(root / "draft-view.png");
                RequireBracketSurface(handedOff && viewRequestObserved && panelRequestObserved && owner->IsViewMode() &&
                    owner->WorkspacePanelOpen() && owner->IsBracketingToolActive() && owner->HasUnsavedBracketingDraft(),
                    "Gallery draft did not open its Bracketing View and Source panel.");
                RequireBracketSurface(!owner->IsBracketingActive() && !owner->IsMultiFrameRawProjectActive() &&
                    owner->GetCurrentProjectFileName().empty() && !owner->GetActiveRawProjectSnapshot() &&
                    !owner->IsActiveMultiFrameProcessingForQueueBusy() && !owner->IsBracketingPresentationActive(),
                    "Draft entry imported or processed a bracket unexpectedly.");
                for (const char* label : {"Bracketing", "Review captures in the Source panel, then choose Process.", "Process"})
                    RequireBracketSurface(capturedText.find(label) != std::string::npos,
                        std::string("Draft View is missing: ") + label);
                RequireBracketSurface(capturedText.find(std::to_string(files.size()) + " captures") != std::string::npos,
                    "Source panel lost the draft capture count.");
                for (const auto& file : files) RequireBracketSurface(capturedText.find(file.filename().string()) != std::string::npos,
                    "Source panel lost selected capture: " + file.filename().string());
                RequireBracketSurface(capturedText.find("Drop an image into the graph") == std::string::npos &&
                    capturedText.find("Reading capture details") == std::string::npos,
                    "Draft View retained a generic graph prompt or did not finish reading metadata.");
                owner->SetWorkspacePanelOpen(false);
                textCapture = root / "draft-panel-hidden-labels.txt";
                frame();
                RequireBracketSurface(capturedText.find("Open Bracketing") != std::string::npos,
                    "A hidden Source panel has no recovery action on the draft canvas.");
                CaptureBracketSurface(root / "draft-panel-hidden.png");
                const auto draftDocument = owner->GetProjectDocumentId();
                foreground = other.get();
                for (unsigned index = 0; index < 8; ++index) frame();
                foreground = owner.get();
                // Both the canvas and sidebar recovery buttons execute this
                // public action. It must restore controls, preserving inputs.
                frameAction = [&] {
                    owner->OpenBracketingTool();
                    RequireBracketSurface(owner->WorkspacePanelOpen(),
                        "Opening Bracketing did not immediately reveal its controls.");
                };
                frame();
                frame(); // Consume the reopened Source request as the shell does.
                // The compositor retains outgoing project textures for its
                // finite switch fade. Capture the settled owner, not that blend.
                const auto controlsCaptureAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                while (std::chrono::steady_clock::now() < controlsCaptureAt) frame();
                textCapture = root / "reopened-controls-labels.txt";
                frame();
                RequireBracketSurface(owner->GetProjectDocumentId() == draftDocument &&
                    owner->HasUnsavedBracketingDraft() && owner->WorkspacePanelOpen(),
                    "Opening Bracketing replaced the draft or left Source hidden.");
                RequireBracketSurface(capturedText.find(std::to_string(files.size()) + " captures") != std::string::npos &&
                    capturedText.find("[ Process ]") != std::string::npos &&
                    capturedText.find("Select captures in the filmstrip or Gallery.") == std::string::npos,
                    "Opening Bracketing did not restore usable draft controls.");
                for (const auto& file : files) RequireBracketSurface(capturedText.find(file.filename().string()) != std::string::npos,
                    "Opening Bracketing lost capture: " + file.filename().string());
                CaptureBracketSurface(root / "reopened-controls.png");
                std::cout << "PASS Gallery Create Capture Set before import, selected originals, requested View/Source panel, "
                    "contextual canvas, independent project switch and reopened Bracketing controls; no bracket import or Process (" << frames
                    << " submitted frames)\nCaptures: " << root << '\n' << std::flush;
                success = true;
            } else {
            if (reopenOnly) {
                const auto opened = Project::ProjectOpenCoordinator::Load(std::filesystem::u8path(argv[2]));
                RequireBracketSurface(bool(opened), opened.error);
                RequireBracketSurface(owner->BeginDeferredLoadedProjectApply(opened.candidate),
                    "Existing scratch bracket reopen was rejected.");
            } else {
                owner->BeginBracketingDraft(true);
                owner->AddBracketingDraftFiles(files);
                bool imported = false;
                wait([&] {
                    if (imported) return true;
                    std::string error;
                    if (owner->CommitBracketingDraft(false, &error)) {
                        imported = true;
                        return true;
                    }
                    RequireBracketSurface(error == "Wait for capture metadata inspection to finish.", error);
                    return false;
                }, "Bracket capture metadata/import timed out.");
            }
            wait([&] {
                RequireBracketSurface(!owner->HasDeferredLoadedProjectApplyFailed(),owner->GetDeferredLoadedProjectStatusText());
                return owner->HasDeferredLoadedProjectApplyCoreFinished()&&!owner->IsDeferredLoadedProjectApplyActive();
            },
                "Bracket project activation timed out.");
            frame();
            RequireBracketSurface(owner->IsBracketingActive(), "Imported bracket session was not activated.");
            owner->OpenBracketingTool();
            if(reopenOnly)owner->SetWorkspaceMode(Project::WorkspaceMode::View);
            std::string error;
            if (!reopenOnly) {
                RequireBracketSurface(owner->CommitBracketingDraft(true, &error), error);
                RequireBracketSurface(owner->IsBracketingPresentationActive(), "Process did not lock its owning project.");
                RequireBracketSurface(!owner->SetWorkspaceMode(Project::WorkspaceMode::Graph),
                    "A processing project allowed Graph editing.");
                for (unsigned index = 0; index < 8; ++index) frame();
                CaptureBracketSurface(root / "processing.png");
            }
            foreground = other.get();
            for (unsigned index = 0; index < 12; ++index) frame();
            RequireBracketSurface(!other->IsBracketingPresentationActive() &&
                other->GetProjectDocumentId() == otherId && other->SerializePipeline() == otherPipeline,
                "Bracketing processing changed or locked another project's state.");
            foreground = owner.get();
            wait([&] {
                RequireBracketSurface(!owner->DidActiveMultiFrameProcessingForQueueFail(&error), error);
                return !owner->IsActiveMultiFrameProcessingForQueueBusy() &&
                    !owner->IsBracketingPresentationActive();
            }, "Single bracket process/display handoff timed out.", 600);
            RequireBracketSurface(owner->IsBracketingToolActive() && owner->IsViewMode(),
                "Completion did not open the Bracketing source in normal View.");
            for (unsigned index = 0; index < 8; ++index) frame();
            CaptureBracketSurface(root / "completed-view.png");
            const auto completedProjectPath=owner->GetCurrentProjectFileName();
            std::cout<<"Scratch bracket project: "<<completedProjectPath<<'\n'<<std::flush;
            bool resultSaved=false,resultSaveFinished=false;
            RequireBracketSurface(owner->RequestSaveCurrentProject("Bracket result",[&](bool ok) {
                resultSaved=ok;resultSaveFinished=true;
            }),"Completed bracket save was rejected.");
            wait([&]{return resultSaveFinished;},"Completed bracket save timed out.");
            RequireBracketSurface(resultSaved,"Completed bracket save failed.");
            owner->OpenRawLayerMaskGraph({}, false);
            CheckBracketSource(owner->GetNodeGraph());
            RequireBracketSurface(owner->SetWorkspaceMode(Project::WorkspaceMode::Graph),
                "Finished bracket did not permit Graph navigation.");
            for (unsigned index = 0; index < 12; ++index) frame();
            CaptureBracketSurface(root / "first-graph.png");
            int sourceNode = -1;
            for (const auto& node : owner->GetNodeGraph().GetNodes())
                if (node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr) sourceNode = node.id;
            frameAction=[&] {
                RequireBracketSurface(sourceNode >= 0 && owner->OpenRawProjectNodeEditor(sourceNode),
                    "The first graph source did not open its Bracketing View.");
            };
            frame();
            for (unsigned index = 0; index < 8; ++index) frame();
            frameAction=[&]{owner->ReturnToGraphFromNode();};
            frame();
            const auto transitionCaptureAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(240);
            while(owner->IsReturningToGraph()&&std::chrono::steady_clock::now()<transitionCaptureAt)frame();
            CaptureBracketSurface(root / "return-to-graph.png");
            wait([&] { return !owner->IsReturningToGraph(); }, "Source-to-Graph return did not settle.");
            RequireBracketSurface(!owner->IsViewMode(),"Source return did not reach Graph.");
            const auto* snapshot = owner->GetActiveRawProjectSnapshot();
            RequireBracketSurface(snapshot != nullptr, "Completed bracket lost its document.");
            const auto projectId = snapshot->projectId;
            const auto revision = snapshot->hdrInputRevision;
            const auto projectPath = owner->GetCurrentProjectFileName();
            std::cout << "Scratch bracket project: " << projectPath << '\n' << std::flush;
            bool saved = false, saveFinished = false;
            RequireBracketSurface(owner->RequestSaveCurrentProject("Bracket node surface", [&](bool ok) {
                saved = ok; saveFinished = true;
            }), "Bracket save was rejected.");
            wait([&] { return saveFinished; }, "Bracket save timed out.");
            RequireBracketSurface(saved, "Bracket save failed.");
            const auto opened = Project::ProjectOpenCoordinator::Load(projectPath);
            RequireBracketSurface(bool(opened), opened.error);
            RequireBracketSurface(owner->BeginDeferredLoadedProjectApply(opened.candidate),
                "Saved bracket reopen was rejected.");
            wait([&] {
                RequireBracketSurface(!owner->HasDeferredLoadedProjectApplyFailed(),owner->GetDeferredLoadedProjectStatusText());
                RequireBracketSurface(!owner->DidActiveMultiFrameProcessingForQueueFail(&error),error);
                return owner->HasDeferredLoadedProjectApplyCoreFinished()&&!owner->IsDeferredLoadedProjectApplyActive()&&
                    !owner->IsActiveMultiFrameProcessingForQueueBusy();
            }, "Saved bracket restore timed out.");
            owner->OpenBracketingTool();
            owner->SetWorkspaceMode(Project::WorkspaceMode::View);
            for (unsigned index = 0; index < 8; ++index) frame();
            const auto* restored = owner->GetActiveRawProjectSnapshot();
            RequireBracketSurface(restored && restored->projectId == projectId &&
                restored->hdrInputRevision == revision, "Reopening changed bracket identity or requested fresh input processing.");
            const auto restoredGraph = owner->BuildGraphSnapshot();
            RequireBracketSurface(std::any_of(restoredGraph.nodes.begin(), restoredGraph.nodes.end(),
                [](const auto& node) { return bool(node.rawDevelopment.embeddedRawData); }),
                "Saved bracket did not restore its accepted image source.");
            owner->OpenRawLayerMaskGraph({}, false);
            CheckBracketSource(owner->GetNodeGraph());
            CaptureBracketSurface(root / "reopened-view.png");
            const auto* sourceSet = Project::FindSourceSet(*restored, restored->activeSourceSetId);
            Raw::Bracketing::BracketingRecipe recipe;
            RequireBracketSurface(sourceSet && Raw::Bracketing::Deserialize(
                sourceSet->settings.at("bracketing"), recipe, error), error);
            const auto enabledGroups = std::count_if(recipe.groups.begin(), recipe.groups.end(),
                [](const auto& group) { return group.enabled && std::any_of(group.frames.begin(),
                    group.frames.end(), [](const auto& frame) { return frame.enabled; }); });
            if (enabledGroups < 2) RequireBracketSurface(!owner->HasBracketingContributionTool(),
                "A single enabled exposure group exposed a contribution editor.");
            std::cout << "PASS " << (reopenOnly ? "saved-result recapture" : "one bracket Process and owning-project lock")
                << ", independent project switch, normal View/Graph/panel captures, compact source, contribution eligibility, save and reopen ("
                << frames << " submitted frames)\nCaptures and project: " << root << '\n' << std::flush;
            success = true;
            }
        } catch (const std::exception& error) {
            std::cerr << "Bracket node surface validation failed: " << error.what() << '\n' << std::flush;
        }
        compositor.Shutdown();
    }
    for (auto* editor : {owner.get(), other.get()}) editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();
    other.reset(); owner.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window); glfwTerminate();
    return success;
}

// A visual check of the contribution widget using a real saved recipe. This
// deliberately reads only the source-set JSON: it does not open, migrate,
// process, save or modify the project or any of its captures.
bool ValidateBracketingContributionUi(const std::filesystem::path& savedProjectJson,
    const std::filesystem::path& outputImage) {
    Editor::BracketingSession session;
    try {
        std::ifstream input(savedProjectJson, std::ios::binary);
        RequireBracketSurface(bool(input), "Could not read the saved bracket recipe.");
        const auto saved = nlohmann::json::parse(input);
        bool found = false;
        for (const auto& sourceSet : saved.at("sourceSets")) {
            const auto settings = sourceSet.find("settings");
            if (settings == sourceSet.end() || !settings->contains("bracketing")) continue;
            std::string error;
            RequireBracketSurface(Raw::Bracketing::Deserialize(settings->at("bracketing"),
                session.recipe, error), error);
            found = true;
            break;
        }
        RequireBracketSurface(found, "The JSON contains no saved Bracketing recipe.");
        Editor::PrepareBracketingControls(session);
        RequireBracketSurface(session.controls.hasContributions,
            "This recipe has no multiple enabled exposure levels to combine.");
    } catch (const std::exception& error) {
        std::cerr << "Bracket contribution preview failed: " << error.what() << '\n';
        return false;
    }
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(420, 400, "Bracketing contribution preview", nullptr, nullptr);
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
    bool success = false;
    try {
        for (unsigned frame = 0; frame < 3; ++frame) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const auto size = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 8});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            if (ImGui::Begin("##BracketContributionPreview", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground)) {
                Editor::DrawBracketingContributionTool(session);
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::Render();
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            const auto clear = appearance.GetClearColor();
            glClearColor(clear.x, clear.y, clear.z, clear.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 0; y < height / 2; ++y)
            for (int x = 0; x < width * 4; ++x)
                std::swap(pixels[std::size_t(y) * width * 4 + x],
                    pixels[std::size_t(height - y - 1) * width * 4 + x]);
        if (!outputImage.parent_path().empty()) std::filesystem::create_directories(outputImage.parent_path());
        RequireBracketSurface(stbi_write_png(outputImage.string().c_str(), width, height, 4,
            pixels.data(), width * 4) != 0, "Could not save the contribution preview.");
        std::cout << "PASS native contribution widget from saved recipe (" << width << 'x' << height
            << ")\nCapture: " << outputImage << '\n';
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "Bracket contribution preview failed: " << error.what() << '\n';
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return success;
}
}
