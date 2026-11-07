#include "Raw/Bracketing/SourceSetIdentity.h"
#include "App/Validation/ValidationSuites.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Editor/UI/EditorViewport.h"
#include "Editor/UI/ViewComparisonLayout.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Renderer/GLLoader.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Renderer/WorkspaceCompositor.h"
#include "Notifications/NotificationStore.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "App/WorkspaceSwitcher.h"
#include "ThirdParty/stb_image_write.h"
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
void ValidateRawGraphInsertions(EditorModule& editor);

static bool ValidateGallery(const std::string& rawPath, const std::string& scratchPath, bool persistenceChecks,
    bool nodeTransitionChecks, bool halationChecks,bool profileOnly,
    const std::string& firstShot = {}, const std::string& secondShot = {}, bool checkAutoIntake = false, bool jumpOnly = false,
    bool immediateOpen = false) {
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
    const bool renderInterface=nodeTransitionChecks || halationChecks;
    const bool renderGraphics=renderInterface || !persistenceChecks;
    if (renderGraphics) ImGui_ImplOpenGL3_Init("#version 430");
    else ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    Async::TaskSystem::Get().Initialize();
    auto editor = std::make_unique<EditorModule>();
    std::unique_ptr<EditorModule> gallery;
    bool success = false;
    try {
        // Scan only an isolated copy: Gallery discovery writes its own catalog.
        const auto scratch=std::filesystem::absolute(std::filesystem::u8path(scratchPath));
        const auto root=(profileOnly || immediateOpen)?scratch:scratch/Project::GenerateStableUuid();
        std::filesystem::create_directories(root);
        const auto input = std::filesystem::absolute(std::filesystem::u8path(rawPath));
        const bool bracketCaptures = !profileOnly && std::filesystem::is_directory(input);
        const bool bracketProject = input.extension() == ".stack";
        const bool savingOriginal = persistenceChecks && !bracketCaptures && !bracketProject;
        std::vector<std::filesystem::path> captures;
        if (bracketCaptures) {
            for (const auto& entry : std::filesystem::directory_iterator(input))
                if (entry.is_regular_file() && entry.path().extension()==".dng") captures.push_back(entry.path());
            std::sort(captures.begin(),captures.end());
            if (captures.size()<2) throw std::runtime_error("Bracket comparison needs two DNG captures");
            captures.resize(2);
            for (auto& path : captures) {
                const auto copy=root/path.filename();std::filesystem::copy_file(path,copy);path=copy;
            }
        } else if (!profileOnly && !immediateOpen) std::filesystem::copy_file(input, root / input.filename());
        std::cout << "Diagnostic directory: " << root << '\n' << std::flush;
        editor->SetDocumentPersistenceEnabled(false);
        editor->SetWorkspaceAppStatePersistenceEnabled(false);
        editor->Initialize(window, nullptr, false, immediateOpen || savingOriginal);
        editor->SetPermanentGalleryWorkspace(profileOnly);
        editor->EnterRawWorkspaceRootTab();
        if (!immediateOpen && !savingOriginal) editor->SetRawWorkspaceFolder(root);

        Stack::Renderer::WorkspaceCompositor compositor;
        Stack::Workspace::Switcher switcher;
        bool measureFrames=false;
        bool autoRequested=false;
        const auto frame = [&](const std::function<void()>& action = {}) {
            glfwPollEvents();
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            if (renderGraphics) ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            editor->PumpNonRenderingWork(2.5);
            if (checkAutoIntake && !autoRequested) {
                editor->SetDocumentPersistenceEnabled(true);
                editor->TickAutoBracketing(false);
                editor->SetDocumentPersistenceEnabled(false);
                autoRequested = editor->ConsumeAutoBracketWorkspaceRequest();
                if (autoRequested && (editor->IsRawWorkspaceScanBusy() || editor->IsRawWorkspaceThumbnailBusy()))
                    throw std::runtime_error("Auto bracketing requested a worker before preparation finished.");
            }
            if (bracketCaptures || bracketProject || (persistenceChecks && editor->IsBracketingActive()))
                editor->TickBracketing();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({1000, 700});
            ImGui::Begin("RAW diagnostic", nullptr,
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
            if (action) action();
            if (renderInterface) {
                editor->UpdateNodeOpenTransition();
                const auto origin = ImGui::GetCursorScreenPos();
                const auto size = ImGui::GetContentRegionAvail();
                editor->SetGraphReturnBounds(origin, {origin.x+size.x, origin.y+size.y});
                editor->RenderUI();
            } else editor->RenderRawWorkspaceLabUI();
            ImGui::End();
            if (renderGraphics) {
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
            if (!measureFrames) std::this_thread::sleep_for(std::chrono::milliseconds(3));
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
            while (!ready() && std::chrono::steady_clock::now() - begin < std::chrono::seconds(bracketCaptures ? 180 : 60)) {
                frame();
                if (std::chrono::steady_clock::now() >= nextReport) {
                    report();
                    nextReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                }
            }
            report();
            if (!ready()) throw std::runtime_error(failure);
        };
        if (profileOnly && !firstShot.empty() && secondShot.empty()) {
            const auto opened = std::chrono::steady_clock::now();
            auto requested = opened;
            bool found = false, indexed = false;
            int width = 0, height = 0;
            unsigned int texture = 0;
            while (!texture && std::chrono::steady_clock::now() - opened < std::chrono::seconds(30)) {
                frame([&] {
                    const auto& records = editor->GetRawWorkspaceState().sources;
                    const auto item = std::find_if(records.begin(), records.end(),
                        [&](const auto& source) { return source.relativePathKey == firstShot; });
                    if (item == records.end()) return;
                    if (!found) {
                        requested = std::chrono::steady_clock::now();
                        std::cout << "Jump target available at item " << (item - records.begin() + 1) << " of " << records.size()
                            << " after " << std::chrono::duration_cast<std::chrono::milliseconds>(requested-opened).count() << " ms.\n";
                        found = true;
                    }
                    texture = editor->GetRawWorkspaceThumbnailTexture(*item, &width, &height, false);
                    indexed = !item->fingerprint.empty();
                });
            }
            if (!texture || !width || !height) {
                report();
                std::cout << "Jump root=" << editor->GetRawWorkspaceState().workspaceRoot
                    << " scan status=" << editor->GetRawWorkspaceScanProgress().statusText << '\n';
                throw std::runtime_error("Distant Gallery tile did not become resident.");
            }
            std::cout << "Distant tile " << width << "x" << height << " resident "
                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-requested).count()
                << " ms after request, verified identity=" << indexed << ", scan busy=" << editor->IsRawWorkspaceScanBusy() << ".\n";
        }
        if (profileOnly && !jumpOnly) {
            const auto pairIsVisible = [&] {
                const auto& stacks = editor->ResolveRawWorkspaceVisibleGalleryStacks();
                return std::any_of(stacks.begin(), stacks.end(), [&](const auto& stack) {
                    const auto has = [&](const std::string& key) {
                        return std::find(stack.sourceKeys.begin(), stack.sourceKeys.end(), key) != stack.sourceKeys.end();
                    };
                    return has(firstShot) && has(secondShot);
                });
            };
            if (!secondShot.empty()) {
                using namespace Stack::RawWorkspace;
                if (firstShot == secondShot) throw std::runtime_error("Specify two distinct capture keys.");
                bool sawPreviewStack = false;
                bool cached = false;
                const auto start = std::chrono::steady_clock::now();
                wait([&] {
                    const auto& sources = editor->GetRawWorkspaceState().sources;
                    const auto find = [&](const std::string& key) {
                        return std::find_if(sources.begin(), sources.end(),
                            [&](const auto& source) { return source.relativePathKey == key; });
                    };
                    const auto first = find(firstShot), second = find(secondShot);
                    if (first == sources.end() || second == sources.end()) return false;
                    if (!pairIsVisible()) return false;
                    const auto confirmed = [](const auto& source) { return source.thumbnail.similarityDescriptor.IsValid(); };
                    if (!sawPreviewStack && !confirmed(*first) && !confirmed(*second) &&
                        first->transientThumbnail.similarityDescriptor.IsValid() && second->transientThumbnail.similarityDescriptor.IsValid()) {
                        sawPreviewStack = true;
                        std::cout << "Matching camera tiles formed a visible stack at "
                            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                            << " ms, before neutral analysis of either capture.\n";
                    }
                    if (editor->IsRawWorkspaceScanBusy() || editor->IsRawWorkspaceThumbnailBusy() ||
                        !confirmed(*first) || !confirmed(*second)) return false;
                    cached = first->thumbnail.status == ThumbnailStatus::Valid &&
                        second->thumbnail.status == ThumbnailStatus::Valid;
                    if (!cached && !sawPreviewStack)
                        throw std::runtime_error("The pair only grouped after neutral RAW analysis.");
                    return true;
                }, "Expected camera pair did not form a visible stack.");
                std::cout << "Confirmed visible stack from " << (cached ? "cached" : "new") << " neutral descriptors.\n";
            }
            wait([&]{return !editor->IsRawWorkspaceScanBusy() && !editor->GetRawWorkspaceState().sources.empty();},
                "Warm Gallery scan timed out");
            const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(5);
            do {frame();} while (editor->IsRawWorkspaceThumbnailBusy() && std::chrono::steady_clock::now()<until);
            if (editor->IsRawWorkspaceThumbnailBusy()) throw std::runtime_error("Gallery thumbnails did not settle.");
            for (int i=0;i<20;++i) frame();
            const auto& workspace = editor->GetRawWorkspaceState();
            const bool resultOnlyPair = checkAutoIntake && workspace.sources.size() == 2 &&
                workspace.sourceSetProjects.size() == 1 && workspace.sourceSetProjects.front().standaloneMaster;
            if (resultOnlyPair) {
                std::vector<Raw::Bracketing::CaptureContentIdentity> captures;
                for (const auto& source : workspace.sources) captures.push_back({source.fingerprint, source.fileSizeBytes});
                const auto expected = Raw::Bracketing::SourceSetIdentity(captures);
                if (expected.empty() || workspace.sourceSetProjects.front().masterCaptureSetIdentity != expected)
                    throw std::runtime_error("The discovered master did not identify its exact pair of captures.");
                const float step = ImGui::GetIO().DeltaTime;
                ImGui::GetIO().DeltaTime = 1.f;
                for (int i=0; i<90 && !autoRequested; ++i) frame();
                ImGui::GetIO().DeltaTime = step;
                if (autoRequested || std::filesystem::exists(root / "Closet"))
                    throw std::runtime_error("Automatic intake attempted a pair with a surviving master.");
                std::cout << "PASS rediscovered master matched both captures and suppressed automatic processing.\n";
            } else if (checkAutoIntake) {
                wait([&] {return autoRequested;}, "Ready Gallery did not request automatic bracket processing.");
                if (std::filesystem::exists(root / "Closet")) throw std::runtime_error("Browsing or scheduling created a Closet.");
                std::cout << "Automatic bracket worker requested after readiness without creating a Closet.\n";
                auto worker = std::make_unique<EditorModule>();
                worker->SetWorkspaceAppStatePersistenceEnabled(false);
                worker->SetDocumentPersistenceEnabled(false);
                worker->Initialize(window, nullptr, false);
                const bool adopted = editor->TransferAutoBracketingTo(*worker) && !worker->IsRawWorkspaceScanBusy() &&
                    worker->GetRawWorkspaceState().sources.size() == editor->GetRawWorkspaceState().sources.size();
                if (adopted) frame([&] { worker->TickAutoBracketing(false); });
                const bool started = adopted && worker->AutoBracketWorkActive() && Stack::RawWorkspace::IsMarkedCloset(root / "Closet");
                worker->CancelAutoBracketingForWorkspaceClose();
                worker->Shutdown();
                if (!started) throw std::runtime_error("Prepared bracket worker did not start on its first tick.");
                std::cout << "Worker started on first tick with the prepared catalog and initialized Closet. Test job stopped.\n";
            }
            const auto before=editor->GetRawWorkspaceScanProgress();
            const auto* presentation=&editor->GetRawWorkspaceGalleryPresentation();
            measureFrames=true;
            const auto start=std::chrono::steady_clock::now();
            for (int i=0;i<120;++i) {
                frame();
                if (&editor->GetRawWorkspaceGalleryPresentation()!=presentation || editor->IsRawWorkspaceScanBusy())
                    throw std::runtime_error("An idle frame restarted Gallery discovery.");
            }
            glFinish();
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto after=editor->GetRawWorkspaceScanProgress();
            if (!secondShot.empty() && !pairIsVisible()) throw std::runtime_error("Settled Gallery split the capture pair.");
            if (before.hashedRawCount!=after.hashedRawCount || before.verifiedRawBytes!=after.verifiedRawBytes)
                throw std::runtime_error("An idle Gallery frame hashed media.");
            std::cout<<"Warm Gallery: "<<editor->GetRawWorkspaceState().sources.size()<<" originals, 120 frames, "
                <<ms/120<<" ms/frame including GPU submission and completion; no new source hashes or scan.\n";
        } else if (immediateOpen) {
            if (std::filesystem::exists(root / "Closet")) throw std::runtime_error("Expected a disposable photo root without a Closet.");
            auto notices = std::make_shared<Notifications::NotificationStore>();
            editor->SetNotificationScope(notices->ForOwner(1, "Untitled"));
            gallery = std::make_unique<EditorModule>();
            gallery->SetWorkspaceAppStatePersistenceEnabled(false);
            gallery->SetDocumentPersistenceEnabled(false);
            gallery->Initialize(window, nullptr, false, true);
            gallery->SetPermanentGalleryWorkspace(true);
            gallery->SetRawWorkspaceFolder(root);
            bool clicked = false;
            gallery->SetRawGalleryOpenSelectionHandler([&](auto selection, bool bracket) {
                if (bracket || selection.items.size() != 1 || selection.items.front().path != input ||
                    selection.items.front().sourceWorkspaceRoot != root)
                    throw std::runtime_error("Wrong original dispatched by Gallery.");
                clicked = true;
                const auto& item = selection.items.front();
                editor->OpenGalleryOriginalInNewTab(item.sourceWorkspaceRoot, item.path);
            });
            const auto key = input.lexically_relative(root).generic_u8string();
            wait([&] { return !gallery->GetRawWorkspaceState().sources.empty(); }, "Gallery published no originals.");
            std::cout << "Clicked " << key << " with folder scan busy=" << gallery->IsRawWorkspaceScanBusy() << '\n';
            gallery->SelectRawWorkspaceSourceForGallery(key, false, false, true);
            if (!clicked) throw std::runtime_error("Gallery did not dispatch the clicked original.");
            const bool raster = RawWorkspace::IsGalleryRasterPath(input);
            wait([&] {
                if (editor->GalleryLoadFailed()) throw std::runtime_error("The selected original failed to load.");
                return raster ? editor->GetNodeGraph().IsOutputConnected() && editor->GetViewportOutputRenderGeneration() != 0 :
                    editor->HasActiveRawWorkspacePresentationForValidation();
            }, "The clicked original never rendered.");
            const auto& records = editor->GetRawWorkspaceState().sources;
            // The transient listing no longer pre-hashes the original. Source
            // verification belongs to the decoder, which produced this frame.
            if (records.size() != 1 || records.front().absolutePath != input ||
                editor->IsRawWorkspaceScanBusy() || editor->GetRawWorkspaceScanProgress().filesVisited != 0 ||
                std::filesystem::exists(root / "Closet")) throw std::runtime_error("Opening the original scanned or wrote the photo root.");
            for (const auto& notice : notices->Snapshot())
                if (notice.content.severity == Notifications::Severity::Error)
                    throw std::runtime_error(notice.content.message);
            std::cout << "PASS immediate Gallery open: selected original rendered, no folder rescan, error notification or Closet.\n";
            gallery->SetRawGalleryOpenSelectionHandler({});
        } else if (!profileOnly) {
        if (bracketCaptures) {
            wait([&] {return !editor->IsRawWorkspaceScanBusy() && editor->GetRawWorkspaceState().sources.size()>=2;},"Capture scan timed out");
            editor->BeginBracketingDraft(true);
            editor->AddBracketingDraftFiles(captures);
            std::string error;
            bool committed=false;
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while (!committed && std::chrono::steady_clock::now()<until) {
                frame([&] {editor->TickBracketingDraft();committed=editor->CommitBracketingDraft(true,&error);});
                if (!committed && error!="Wait for capture metadata inspection to finish.")
                    throw std::runtime_error(error);
            }
            if (!committed) throw std::runtime_error(error);
        } else if (bracketProject) {
            auto loaded = Project::ProjectOpenCoordinator::Load(root / input.filename());
            if (!loaded) throw std::runtime_error("Could not load bracket comparison fixture: " + loaded.error);
            if (!editor->BeginDeferredLoadedProjectApply(loaded.candidate))
                throw std::runtime_error("Could not apply bracket comparison fixture: " + editor->GetDeferredLoadedProjectStatusText());
        } else if (savingOriginal) {
            editor->OpenGalleryOriginalInNewTab(root, root / input.filename());
        } else {
        wait([&] { return !editor->IsRawWorkspaceScanBusy() &&
            !editor->GetRawWorkspaceState().sources.empty(); }, "Gallery scan timed out");
        const auto key = editor->GetRawWorkspaceState().sources.front().relativePathKey;
        // This is the same public action used by the shell after a Gallery open
        // creates a project tab and that tab's source catalog is ready.
        editor->SelectRawWorkspaceSourceForGallery(key, false, false, true);
        }
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
        if (!persistenceChecks && !nodeTransitionChecks && !halationChecks) {
            // Exercise the production split request through loading frames.
            // Previously PrepareRawSplitComparison canceled every pending job.
            EditorViewport comparison;
            editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::View);
            bool shown=false;
            const auto compareFrame=[&] {
                frame([&] {
                    ImGui::SetWindowFocus();
                    shown=comparison.PrepareRawSplitComparison(editor.get(),{1000,700});
                });
            };
            ImGui::GetIO().AddMousePosEvent(850,350);
            compareFrame();compareFrame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_S,true);compareFrame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_S,false);
            const auto began=std::chrono::steady_clock::now();
            while(!shown && std::chrono::steady_clock::now()-began<std::chrono::seconds(30))compareFrame();
            if(!shown)throw std::runtime_error("Requested split comparison never published its before image");
            std::cout << "Original comparison ready in " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-began).count() << " ms\n" << std::flush;
            for(int i=0;i<20;++i)compareFrame();
            ImVec2 left{},right{},deltaA={12,8},deltaB=deltaA;
            frame([&] {
            const float fit = Editor::RawViewFitFraction * std::min(1000.f / 1600.f, 700.f / 1200.f);
            const ImVec2 fittedSize(1600 * fit, 1200 * fit);
            const auto layout=Editor::ViewComparisonLayout::Resolve({0,0},{1000,700},
                ImGui::GetWindowDrawList()->GetClipRectMin(),ImGui::GetWindowDrawList()->GetClipRectMax(),
                fittedSize,1.f);
            left=layout.Present({400,250},false);right=layout.Present({400,250},true);
            comparison.MapRawComparisonInput(left,deltaA,{0,0},{1000,700},fittedSize);
            comparison.MapRawComparisonInput(right,deltaB,{0,0},{1000,700},fittedSize);
            });
            if(std::abs(deltaA.x-deltaB.x)>.001f || std::abs(deltaA.y-deltaB.y)>.001f ||
                std::abs(left.x-right.x)>.001f || std::abs(left.y-right.y)>.001f)
                throw std::runtime_error("Split panning differs between sides");
            std::cout << "PASS requested split loads original; both panes share pan scale\n" << std::flush;
            // Let the actual preview finish its request before the capture.
            for(int i=0;i<240;++i)frame();
            const auto* backdrop=editor->GetRawImageBackdrop();
            if(backdrop && backdrop->sharpDrawList)
                throw std::runtime_error("Native split did not keep both full photo edges");
            std::vector<unsigned char> capture(1000u*700u*4u);
            glReadPixels(0,0,1000,700,GL_RGBA,GL_UNSIGNED_BYTE,capture.data());
            for(int y=0;y<350;++y) for(int x=0;x<4000;++x)
                std::swap(capture[y*4000+x],capture[(699-y)*4000+x]);
            const auto image=root/"split-view.png";
            if(!stbi_write_png(image.string().c_str(),1000,700,4,capture.data(),4000))
                throw std::runtime_error("Could not save the native split preview");
            std::cout << "Split preview: " << image << '\n';
            // Exercise both directions and a pan through the actual canvas.
            ImGui::GetIO().AddMousePosEvent(750,350);
            ImGui::GetIO().AddMouseWheelEvent(0,-6);frame();
            for(int i=0;i<30;++i)frame();
            ImGui::GetIO().AddMouseWheelEvent(0,14);frame();
            for(int i=0;i<45;++i)frame();
            ImGui::GetIO().AddMouseButtonEvent(2,true);frame();
            ImGui::GetIO().AddMousePosEvent(810,390);frame();
            ImGui::GetIO().AddMouseButtonEvent(2,false);frame();
            for(int i=0;i<20;++i)frame();
            glReadPixels(0,0,1000,700,GL_RGBA,GL_UNSIGNED_BYTE,capture.data());
            for(int y=0;y<350;++y) for(int x=0;x<4000;++x)
                std::swap(capture[y*4000+x],capture[(699-y)*4000+x]);
            if(!stbi_write_png((root/"split-zoomed.png").string().c_str(),1000,700,4,capture.data(),4000))
                throw std::runtime_error("Could not save zoomed split preview");
        }
        if (halationChecks) {
            const auto require=[](bool ok,const std::string& reason) {
                if (!ok) throw std::runtime_error(reason);
            };
            require(editor->LeaveRawWorkspaceRootTab(true),"Could not enter Graph for connection validation");
            editor->OpenRawLayerMaskGraph({});
            ValidateRawGraphInsertions(*editor);
            const auto checkCurrentPresentation=[&](std::uint64_t before,const char* phase) {
                const auto failure=std::string(phase)+": the changed RAW graph did not publish a current image";
                wait([&] {
                    return editor->GetViewportOutputRenderGeneration()>before &&
                        !editor->IsEditorRenderBusy() && editor->HasCurrentRawWorkspacePresentation();
                },failure.c_str());
                unsigned image=0; int imageWidth=0,imageHeight=0;
                require(editor->TryGetActiveRawWorkspacePresentationTexture(image,imageWidth,imageHeight) &&
                    image && imageWidth>0 && imageHeight>0,
                    std::string(phase)+": no accepted texture; "+editor->GetActiveRawWorkspacePresentationDiagnosticForValidation());
                std::vector<float> result(static_cast<std::size_t>(imageWidth)*imageHeight*4);
                glBindTexture(GL_TEXTURE_2D,image);
                glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data());
                float minimum=1e30f,maximum=-1e30f;
                for (std::size_t i=0;i<result.size();++i) {
                    if (!std::isfinite(result[i])) throw std::runtime_error(std::string(phase)+": nonfinite accepted pixel");
                    if (i%4!=3) {minimum=std::min(minimum,result[i]);maximum=std::max(maximum,result[i]);}
                }
                require(maximum-minimum>.001f,std::string(phase)+": accepted image is blank; "+
                    editor->GetActiveRawWorkspacePresentationDiagnosticForValidation());
                std::cout << "PASS RAW Gallery " << phase << ": generation=" << editor->GetViewportOutputRenderGeneration()
                    << " size=" << imageWidth << 'x' << imageHeight << " RGB range=" << minimum << ".." << maximum
                    << " " << editor->GetActiveRawWorkspacePresentationDiagnosticForValidation() << '\n' << std::flush;
            };
            std::string compoundUuid,error;
            auto before=editor->GetViewportOutputRenderGeneration();
            frame([&] {
                require(editor->LeaveRawWorkspaceRootTab(true),"Could not enter Graph from the opened RAW");
                editor->OpenRawLayerMaskGraph({});
                auto& graph=editor->GetNodeGraph();
                const auto end=std::find_if(graph.GetNodes().begin(),graph.GetNodes().end(),[](const auto& node) {
                    return node.role==Stack::GraphModel::NodeRole::LayerResult &&
                        node.rawLayerOwnerId==Stack::Project::kRawBackgroundId;
                });
                require(end!=graph.GetNodes().end(),"The RAW Background End is missing");
                const auto output=std::find_if(graph.GetNodes().begin(),graph.GetNodes().end(),[](const auto& node) {
                    return node.kind==EditorNodeGraph::NodeKind::Output && node.rawLayerOwnerId.empty();
                });
                require(output!=graph.GetNodes().end(),"The project image output is missing");
                const int endId=output->id;
                const auto* incoming=graph.FindAnyInputLink(endId,"imageIn");
                require(incoming!=nullptr,"The initial RAW image chain is disconnected");
                const auto previous=*incoming;
                const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
                const auto recipe=std::find_if(catalog.begin(),catalog.end(),[](const auto& value) {
                    return value.identity.id=="stack:compound/halation";
                });
                require(recipe!=catalog.end(),"The plain Halation template is absent");
                editor->AddCompoundTemplateNodeAt(static_cast<std::size_t>(recipe-catalog.begin()),{1000,300});
                const int effect=graph.GetSelectedNodeId();
                require(graph.FindNode(effect) && graph.FindNode(effect)->kind==EditorNodeGraph::NodeKind::Compound,
                    "The Halation creation command did not add an instance");
                compoundUuid=graph.FindNode(effect)->instanceUuid;
                require(editor->ConnectGraphSockets(previous.fromNodeId,previous.fromSocketId,effect,"image-in",&error),error);
                require(editor->ConnectGraphSockets(effect,"image-out",endId,"imageIn",&error),error);
                require(!editor->HasCurrentRawWorkspacePresentation(),
                    "The retained image was reported current immediately after connecting Halation");
                require(editor->OpenRawProjectNodeEditor(effect),"Halation did not open in View");
                require(editor->EnterRawWorkspaceRootTab(),"The RAW View handoff failed");
                const auto& labels=editor->GetRawLayerToolLabels();
                const int selected=editor->GetRawLayerToolIndex();
                require(selected>=0 && selected<static_cast<int>(labels.size()) && labels[selected]=="Halation",
                    "Alt navigation did not retain the Halation instance");
            });
            checkCurrentPresentation(before,"packed Halation");
            ImVec2 controlsMin,controlsMax;
            require(editor->GetRawActiveControlBounds(controlsMin,controlsMax),
                "Halation's graph-control fallback did not draw on the View surface");
            before=editor->GetViewportOutputRenderGeneration();
            frame([&] {
                require(editor->LeaveRawWorkspaceRootTab(true),"Could not return to Graph for decompilation");
                editor->OpenRawLayerMaskGraph({});
                auto& graph=editor->GetNodeGraph();
                const auto effect=std::find_if(graph.GetNodes().begin(),graph.GetNodes().end(),[&](const auto& node) {
                    return node.instanceUuid==compoundUuid;
                });
                require(effect!=graph.GetNodes().end(),"The packed instance was lost");
                require(editor->UnpackCompoundNode(effect->id,&error),error);
                require(!editor->HasCurrentRawWorkspacePresentation(),
                    "The retained image was reported current immediately after decompilation");
                require(editor->EnterRawWorkspaceRootTab(),"The decompiled RAW View handoff failed");
            });
            checkCurrentPresentation(before,"one-level decompiled Halation");
            before=editor->GetViewportOutputRenderGeneration();
            frame([&] {
                require(editor->LeaveRawWorkspaceRootTab(true),"Could not return to Graph for repacking");
                editor->OpenRawLayerMaskGraph({});
                const auto& groups=editor->GetNodeGraph().GetGroups();
                const auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& item) {
                    return item.compoundBoundary.is_object() &&
                        item.compoundBoundary.value("instanceUuid",std::string{})==compoundUuid;
                });
                require(group!=groups.end(),"The retained compound boundary is missing");
                require(editor->RepackCompoundGroup(group->id,&error),error);
                require(!editor->HasCurrentRawWorkspacePresentation(),
                    "The retained image was reported current immediately after repacking");
                require(editor->EnterRawWorkspaceRootTab(),"The repacked RAW View handoff failed");
            });
            checkCurrentPresentation(before,"repacked Halation");
        }
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
            if (savingOriginal) require(!std::filesystem::exists(root / "Closet"), "Opening the RAW created a Closet before Save");
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
            const auto versionPath = root / "Closet" / "Projects" / "Live version.stack";
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
            const auto original = root / input.filename();
            const auto renamed = root / ("renamed-" + input.filename().u8string());
            std::filesystem::rename(original, renamed);
            const auto discovery = RawWorkspace::ScanWorkspace(root);
            require(discovery.success && RawWorkspace::WriteCatalogSkeleton(discovery.layout, discovery.sources, {}),
                "Could not index the renamed original");
            auto reopened = Project::ProjectOpenCoordinator::Load(store->StoragePath());
            if (!reopened) throw std::runtime_error("Could not reopen the working project with its indexed original: " + reopened.error);
            auto portableReopened = Project::ProjectOpenCoordinator::Load(portable);
            require(static_cast<bool>(portableReopened), "Self-contained RAW export failed to reopen");
            require(portableReopened.candidate->pipelineData.at("nodeGraph").at("nodes").size() == capturedNodes + 1,
                "Export omitted authored graph edits");
            std::filesystem::rename(renamed, original);
            std::cout << "PASS RAW version shares assets; working and portable projects reopen after original rename\n";
            completed = false; saved = false;
            const auto copyWithLaterEdits = root / "Closet" / "Projects" / "Copy with later edits.stack";
            require(editor->RequestSaveProjectAs(copyWithLaterEdits, [&](bool ok) { saved = ok; completed = true; }), "Save As was rejected");
            editor->AddReformatNodeAt({800, 330});
            wait([&] { return completed; }, "Save As with later edits timed out");
            require(saved && editor->GetProjectDocumentId() == id && editor->IsDirty(),
                "Save As lost later edits or attached them to an older asset store");
            const auto detachedCopy = Project::OpenProjectStore(copyWithLaterEdits);
            require(detachedCopy && detachedCopy.snapshot.pipelineData.at("nodeGraph").at("nodes").size() == capturedNodes + 1,
                "Save As did not preserve its captured revision");
            completed = false; saved = false;
            const auto activatedCopy = root / "Closet" / "Projects" / "Active copy.stack";
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

            const auto bracketPath = root / "Closet" / "Projects" / "Bracket draft.stack";
            std::string bracketError;
            require(editor->CreateMultiFrameRawProject(bracketPath, Project::ProjectStorageKind::WorkingFile,
                "Bracket draft", "Captures", Project::MultiFrameOperationIntent::RawCaptureSet, {original}, 0, &bracketError),
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
            wait([&] { return editor->ProjectTasks().IsIdle(); }, "Bracket metadata inspection timed out");
            editor->TickBracketingDraft();
            completed = false; saved = false;
            require(editor->RequestSaveCurrentProject({}, [&](bool ok) { saved = ok; completed = true; }), "Bracket draft save was rejected");
            wait([&] { return completed; }, "Bracket draft save timed out");
            const auto bracketSaved = Project::OpenProjectStore(bracketPath);
            if (!saved) throw std::runtime_error("Bracket draft save failed: " + editor->GetProjectFileSaveStatusText());
            if (!bracketSaved) throw std::runtime_error("Could not reopen the saved bracket draft: " + bracketSaved.message);
            require(!editor->IsDirty(), "Bracket draft remained dirty after Save");
            const auto* storedSet = Project::FindSourceSet(bracketSaved.snapshot, setId);
            require(storedSet && storedSet->settings.contains("bracketingDraft") &&
                storedSet->settings.at("bracketingDraftSelection").empty() &&
                bracketSaved.snapshot.hdrInputRevision == inputRevision,
                "Saving a bracket draft lost its selection or promoted it to processed inputs");
            require(bracketSaved.store->ResolveAssetFile(assetId) == store->ResolveAssetFile(assetId),
                "Bracket did not share the existing managed source");
            std::cout << "PASS bracket draft saves in the background, retains pending inputs, and shares its RAW\n";
        }
        }
        compositor.Shutdown();
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "RAW Gallery diagnostic failed: " << error.what() << '\n' << std::flush;
    }
    const auto closeStarted = std::chrono::steady_clock::now();
    editor->RequestWorkerShutdownForAppClose();
    if (gallery) gallery->RequestWorkerShutdownForAppClose();
    std::cout << "Close request: " << std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-closeStarted).count() << " ms\n";
    while ((!editor->IsWorkerShutdownReadyForAppClose() || (gallery && !gallery->IsWorkerShutdownReadyForAppClose())) &&
        std::chrono::steady_clock::now()-closeStarted < std::chrono::seconds(30)) {
        Async::TaskSystem::Get().PumpMainThreadTasks(2.5);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    const auto teardownStarted = std::chrono::steady_clock::now();
    if (!editor->IsWorkerShutdownReadyForAppClose() || (gallery && !gallery->IsWorkerShutdownReadyForAppClose())) {
        success=false;std::cerr << "Close background drain timed out\n";
    }
    std::cout << "Close ready after: " << std::chrono::duration<double,std::milli>(
        teardownStarted-closeStarted).count() << " ms\n";
    editor->Shutdown();
    if (gallery) gallery->Shutdown();
    std::cout << "Close final teardown: " << std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-teardownStarted).count() << " ms\n" << std::flush;
    Async::TaskSystem::Get().Shutdown();
    editor.reset();
    gallery.reset();
    if (renderGraphics) ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return success;
}

bool ValidateRawGalleryOpen(const std::string& rawPath,const std::string& scratchPath,bool persistenceChecks,
    bool nodeTransitionChecks,bool halationChecks) {
    return ValidateGallery(rawPath,scratchPath,persistenceChecks,nodeTransitionChecks,halationChecks,false);
}
bool ValidateClosetGallery(const std::string& root, const std::string& firstShot, const std::string& secondShot, bool checkAutoIntake, bool jumpOnly) {
    return ValidateGallery(root,root,false,false,false,true,firstShot,secondShot,checkAutoIntake,jumpOnly);
}
bool ValidateImmediateGalleryOpen(const std::string& original, const std::string& root) {
    return ValidateGallery(original,root,false,false,false,false,{},{},false,false,true);
}

} // namespace Stack::Validation
