#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Async/TaskSystem.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Renderer/GLLoader.h"
#include "BracketingRawToolValidation.h"
#include "BracketingRenderValidation.h"
#include <chrono>
#include <cmath>
#include <vector>
#include <iostream>
#include <thread>

namespace Stack::Validation {
namespace {
void CheckStage(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

std::shared_ptr<EditorLoadedProjectData> MakeLazyRawPreview(
    const RawWorkspace::SourceRecord& source, const std::filesystem::path& root) {
    const auto& path = source.absolutePath;
    auto recipe = RawRecipe::MakeDefaultRecipe(path.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    const int raw = graph.AddRawDevelopmentNode(payload, {})->id;
    const int output = graph.AddOutputNode({300, 0}, true)->id;
    CheckStage(graph.TryConnectSockets(raw, EditorNodeGraph::kImageOutputSocketId,
        output, EditorNodeGraph::kImageInputSocketId), "Could not connect lazy RAW preview");
    auto loaded = std::make_shared<EditorLoadedProjectData>();
    loaded->sourceState = ProjectSourceState::LazyAsset;
    loaded->transientRawPreview = true;
    loaded->projectKind = StackBinaryFormat::kRawProjectKind;
    loaded->projectName = "Lazy preview";
    loaded->projectFileName = (root / "lazy-preview").string();
    loaded->pipelineData = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);
    loaded->rawWorkspaceData = RawWorkspace::BuildRawProjectData(source, recipe, loaded->pipelineData);
    return loaded;
}

void CheckRawGraphPrecision(RenderGraphSnapshot graph) {
    bool hasSource = false;
    for (auto& node : graph.nodes) {
        if (!node.rawDevelopment.embeddedRawData) continue;
        hasSource = true;
        auto& recipe = node.rawDevelopment.recipe;
        recipe.viewTransform.layerJson["enabled"] = false;
        recipe.finishTone.layerJson["enabled"] = false;
        recipe.rgbDenoise.enabled = false;
        recipe.technical.mosaicDenoise.enabled = false;
        recipe.preToneExposureEv = 4.f;
    }
    CheckStage(hasSource, "Precision check has no full RAW source");
    RenderPipeline renderer;
    renderer.Initialize();
    renderer.SetRawDevelopmentAnalysisEnabled(false);
    renderer.SetRawRgbDenoiseAsyncEnabled(false);
    renderer.SetPreviewMaxDimension(0);
    renderer.ExecuteGraph(graph);
    const int width = renderer.GetCanvasWidth(), height = renderer.GetCanvasHeight();
    CheckStage(renderer.GetOutputTexture() && width > 0 && height > 0,
        "Precision check produced no graph output");
    std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4);
    glBindTexture(GL_TEXTURE_2D, renderer.GetOutputTexture());
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    bool headroom = false, finerThanEightBit = false;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        if (i % 4 == 3) continue;
        const float value = pixels[i];
        CheckStage(std::isfinite(value), "Nonfinite RAW graph sample");
        headroom |= value > 1.f;
        finerThanEightBit |= value > 0 && value < 1 &&
            std::abs(value * 255.f - std::round(value * 255.f)) > .01f;
    }
    renderer.Shutdown();
    CheckStage((format == GL_RGBA16F || format == GL_RGBA32F) && headroom && finerThanEightBit,
        "RAW graph lost floating-point precision or highlight headroom");
    std::cout << "PASS RAW graph retains floating-point output, highlight headroom and sub-8-bit steps\n";
}

void CheckPortableRaw(const std::filesystem::path& root, const std::filesystem::path& rawPath) {
    Project::RawProjectSnapshot bootstrap;
    bootstrap.projectId = Project::GenerateStableUuid();
    bootstrap.projectName = "Portable RAW regression";
    bootstrap.projectKindHint = StackBinaryFormat::kRawProjectKind;
    auto created = Project::CreateProjectStore(root / "single", Project::ProjectStorageKind::DirectoryBundle, bootstrap);
    CheckStage(bool(created), created.message);
    const auto transaction = created.store->BeginTransaction(created.snapshot.persistedStorageRevision);
    Project::EmbeddedAssetRecord asset;
    std::string error;
    CheckStage(created.store->StageAssetFile(transaction, rawPath, Project::MultiFrameInputFamily::Raw,
        nlohmann::json::object(), asset, &error), error);
    auto snapshot = created.snapshot;
    snapshot.embeddedAssets.push_back(asset);
    RawWorkspace::SourceRecord source;
    source.absolutePath = root / "unavailable-original.dng";
    source.fileName = rawPath.filename().string();
    source.relativePathKey = source.fileName;
    source.fingerprint = asset.sha256;
    auto recipe = RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.fingerprint = asset.sha256;
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    graph.AddRawDevelopmentNode(payload, {});
    EditorNodeGraph::RawSourcePayload unrelated;
    unrelated.sourcePath = (root / "unrelated-source.dng").string();
    graph.AddRawSourceNode(unrelated, {0, 200});
    graph.AddOutputNode({300, 0}, true);
    snapshot.pipelineData = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);
    snapshot.rawWorkspaceData = RawWorkspace::BuildRawProjectData(source, recipe, snapshot.pipelineData,
        RawWorkspace::RawProjectMode::UnifiedLayers, false);
    snapshot.rawWorkspaceData["managedAssetId"] = asset.assetId;
    const auto committed = created.store->Commit(transaction, snapshot);
    CheckStage(bool(committed), committed.message);
    auto saved = Project::OpenProjectStore(root / "single");
    CheckStage(bool(saved), saved.message);
    auto packed = Project::ConvertProjectStore(saved.store, saved.snapshot, root / "single.stack", Project::ProjectStorageKind::PortableFile);
    CheckStage(bool(packed), packed.message);
    const auto loaded = Project::ProjectOpenCoordinator::Load(root / "single.stack");
    CheckStage(bool(loaded), loaded.error);
    CheckStage(loaded.candidate->decodedRawSource &&
        loaded.candidate->decodedRawSource->metadata.sourceContentSha256 == asset.sha256,
        "Packed RAW did not decode the saved embedded asset");
    for (const auto& node : loaded.candidate->pipelineData["nodeGraph"]["nodes"])
        if (node.value("kind", std::string()) == "RawSource")
            CheckStage(node.value("sourcePath", std::string()) == unrelated.sourcePath,
                "Opening the project rebound an unrelated graph RAW source");
    std::cout << "PASS portable RAW opens with an unavailable original path and matching content identity\n";
}
}

bool ValidateProjectStageState(int argc, char** argv) {
    if (argc != 3 || !glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(640, 480, "Project stage validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {640, 480};
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Async::TaskSystem::Get().Initialize();
    auto editor = std::make_unique<EditorModule>();
    bool success = false;
    try {
        const auto root = std::filesystem::absolute(argv[0]) / Project::GenerateStableUuid();
        std::filesystem::create_directories(root);
        std::cout << "Validation directory: " << root << '\n' << std::flush;
        CheckPortableRaw(root, std::filesystem::absolute(argv[1]));
        BracketingRawToolValidationAccess::PrepareDraftWorkspace(*editor, false);
        editor->Initialize(window);
        BracketingRawToolValidationAccess::SetWorkflowSources(*editor, root, {std::filesystem::absolute(argv[1])});
        const auto frame = [&] {
            ImGui::NewFrame();
            editor->TickBracketing();
            editor->PumpNonRenderingWork(10);
            editor->UpdateBracketingPresentation();
            ImGui::EndFrame();
            Async::TaskSystem::Get().PumpMainThreadTasks();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        };
        const auto wait = [&](auto ready, const char* message) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (!ready() && std::chrono::steady_clock::now() < until) frame();
            CheckStage(ready(), std::string(message) + ": " + editor->GetActiveRawWorkspacePresentationDiagnosticForValidation());
        };
        const auto verifyOutputSwitching = [&] {
            auto& graph = editor->GetNodeGraph();
            const int stage = editor->ResolveRawWorkspaceStageOutputNodeId();
            auto* reformat = graph.AddReformatNode({150, 0});
            reformat->reformatSettings.width = 37;
            reformat->reformatSettings.height = 23;
            const int downstream = reformat->id;
            CheckStage(graph.TryConnect(stage, downstream), "Could not add downstream resize");
            for (const bool connected : {true, false, true, false}) {
                const auto generation = editor->GetViewportOutputRenderGeneration();
                if (connected) CheckStage(editor->ConnectGraphNodes(downstream, graph.GetOutputNodeId()), "Could not reconnect Output");
                else {
                    const auto* input = graph.FindInputLink(graph.GetOutputNodeId(), EditorNodeGraph::kImageInputSocketId);
                    CheckStage(input != nullptr, "Output has no link to remove");
                    const auto link = *input;
                    CheckStage(editor->RemoveGraphLink(link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId),
                        "Could not disconnect Output");
                }
                editor->EnterRawWorkspaceRootTab();
                CheckStage(editor->UsesRawWorkspaceStageRender() != connected, "Wrong RAW viewport render target");
                wait([&] { return editor->GetViewportOutputRenderGeneration() > generation && !editor->IsEditorRenderBusy(); },
                    "Output connection change did not produce a new viewport image");
                if (editor->IsMultiFrameRawProjectActive())
                    BracketingRenderValidationAccess::WaitForIdle(*editor);
                unsigned texture = 0; int width = 0, height = 0;
                CheckStage(editor->TryGetActiveRawWorkspacePresentationTexture(texture, width, height), "No viewport image after Output toggle");
                CheckStage(connected ? width == 37 && height == 23 : width != 37 || height != 23,
                    "Viewport did not switch between downstream resize and RAW stage");
            }
        };
        wait([&] { return BracketingRawToolValidationAccess::MetadataReady(*editor); }, "Metadata inspection timed out");
        std::string error;
        CheckStage(editor->CommitBracketingDraft(false, &error), error);
        wait([&] { return !editor->IsDeferredLoadedProjectApplyActive(); }, "Project activation timed out");
        frame();
        CheckStage(editor->IsBracketingActive(), "Bracket was not activated");
        CheckStage(!editor->GetRawWorkspaceState().sourceSetProjects.empty(), "Saved bracket has no Projects card");
        std::cout << "PASS ordinary bracket Save publishes a Projects card before processing\n";
        const auto originalPath = editor->GetCurrentProjectFileName();
        const auto original = Project::ProjectOpenCoordinator::Load(originalPath);
        CheckStage(bool(original), original.error);
        editor->OpenBracketingTool();
        editor->AddBracketingDraftFiles({std::filesystem::absolute(argv[2])});
        wait([&] { return BracketingRawToolValidationAccess::MetadataReady(*editor); }, "Added capture inspection timed out");
        const auto verifyCopy = [&](const std::filesystem::path& path, Project::ProjectStorageKind kind) {
            CheckStage(editor->SaveActiveMultiFrameRawProjectAs(path, kind, &error), error);
            auto saved = Project::OpenProjectStore(path);
            CheckStage(bool(saved), saved.message);
            CheckStage(saved.snapshot.sourceSets.front().frames.size() == 2 &&
                saved.snapshot.sourceSets.front().settings.contains("bracketingDraft"), "Copy lost pending capture or draft");
            CheckStage(saved.snapshot.hdrInputRevision == original.candidate->rawProjectSnapshot->hdrInputRevision,
                "Copy promoted an unprocessed draft");
            CheckStage((saved.snapshot.projectId == original.candidate->rawProjectSnapshot->projectId) ==
                (kind == Project::ProjectStorageKind::PortableFile), "Save As or Pack has the wrong project identity");
            CheckStage(saved.store->Verify(saved.snapshot), "Copied assets failed verification");
        };
        verifyCopy(root / "bracket.stack", Project::ProjectStorageKind::PortableFile);
        CheckStage(editor->HasPendingBracketingDraft(), "Pack cleared the active draft");
        const auto externalFolder = root.parent_path() / (root.filename().string() + "-exports");
        const auto externalCopy = externalFolder / "bracket-copy";
        verifyCopy(externalCopy, Project::ProjectStorageKind::DirectoryBundle);
        const auto copyId = editor->GetActiveRawProjectSnapshot()->projectId;
        CheckStage(std::none_of(editor->GetRawWorkspaceState().sourceSetProjects.begin(),
            editor->GetRawWorkspaceState().sourceSetProjects.end(),
            [&](const auto& card) { return card.projectId == copyId; }), "External Save As appeared in the old workspace");
        std::vector<RawWorkspace::SourceRecord> sources;
        std::vector<RawWorkspace::SourceSetProjectCatalogEntry> cards;
        const auto layout = RawWorkspace::BuildManagedLayout(externalFolder);
        CheckStage(RawWorkspace::DiscoverProjects(layout, sources) &&
            RawWorkspace::DiscoverSourceSetProjects(layout, sources, cards), "External folder discovery failed");
        const bool discoveredCopy = std::any_of(cards.begin(), cards.end(), [&](const auto& card) { return card.projectId == copyId; });
        if (!discoveredCopy) for (const auto& card : cards)
            std::cerr << "Discovered card: " << card.projectId << " " << card.absolutePath << " " << card.errorMessage << '\n';
        std::cout << "PASS Pack and Save As preserve pending captures and the submitted revision\n";
        CheckStage(editor->ApplyLoadedProject(*original.candidate), "Could not reopen original bracket");
        editor->OpenBracketingTool();
        editor->AddBracketingDraftFiles({std::filesystem::absolute(argv[2])});
        wait([&] { return BracketingRawToolValidationAccess::MetadataReady(*editor); }, "Reopen capture inspection timed out");
        CheckStage(editor->ApplyLoadedProject(*original.candidate), "Could not discard and reopen same project");
        editor->OpenBracketingTool();
        CheckStage(BracketingRawToolValidationAccess::SelectionCount(*editor) == 1,
            "Same-project reopen retained the discarded capture");
        std::cout << "PASS reopening the same project discards its previous in-memory selection\n";
        editor->EnterRawWorkspaceRootTab();
        editor->GetNodeGraph().DisconnectOutput();
        CheckStage(editor->CommitBracketingDraft(true, &error), error);
        wait([&] { return !editor->IsActiveMultiFrameProcessingForQueueBusy() && !editor->IsBracketingPresentationActive(); },
            "Disconnected Output left the bracket or RAW presentation waiting");
        CheckStage(!editor->DidActiveMultiFrameProcessingForQueueFail(&error), error);
        const auto completed = BracketingRawToolValidationAccess::Draft(*editor);
        CheckStage(completed && completed->presentation && completed->presentation->finishedAt >= 0,
            "The bracket overlay closed without presenting the matching RAW result: " +
            editor->GetActiveRawWorkspacePresentationDiagnosticForValidation());
        const int stage = editor->ResolveRawWorkspaceStageOutputNodeId();
        const auto snapshot = editor->BuildGraphSnapshotForTimelineFrame(0, stage);
        CheckStage(stage > 0 && snapshot.outputNodeId == stage && !editor->GetNodeGraph().IsOutputConnected(),
            "RAW stage render changed or required the final graph Output");
        std::cout << "PASS disconnected graph Output allows bracket processing and RAW presentation to complete\n";
        CheckRawGraphPrecision(snapshot);
        BracketingRenderValidationAccess::Run(*editor, window);
        BracketingRawToolValidationAccess::InteractiveCurve(*editor);
        BracketingRenderValidationAccess::WaitForIdle(*editor);
        BracketingRawToolValidationAccess::UpdatedNativeRegion(*editor);
        verifyOutputSwitching();
        std::cout << "PASS bracket viewport follows downstream Output and RAW fallback across repeated toggles\n";
        const auto single = Project::ProjectOpenCoordinator::Load(root / "single.stack");
        CheckStage(bool(single) && editor->ApplyLoadedProject(*single.candidate), "Could not activate portable single RAW");
        editor->EnterRawWorkspaceRootTab();
        const auto priorGeneration = editor->GetViewportOutputRenderGeneration();
        editor->MarkRenderRefreshDirty();
        wait([&] { return editor->GetViewportOutputRenderGeneration() > priorGeneration && !editor->IsEditorRenderBusy(); },
            "Single RAW with disconnected Output did not render");
        unsigned texture = 0; int outputWidth = 0, outputHeight = 0;
        CheckStage(editor->TryGetActiveRawWorkspacePresentationTexture(texture, outputWidth, outputHeight) &&
            outputWidth > 0 && outputHeight > 0, "Single RAW has no displayable RAW stage image");
        CheckStage(!editor->GetNodeGraph().IsOutputConnected(), "Single RAW rendering connected the graph Output");
        std::cout << "PASS portable single RAW renders its stage with the graph Output disconnected\n";
        verifyOutputSwitching();
        std::cout << "PASS single RAW viewport follows downstream Output and RAW fallback across repeated toggles\n";
        CheckStage(!editor->GetRawWorkspaceState().sources.empty(), "The fixture gallery has no RAW sources");
        CheckStage(editor->BeginDeferredLoadedProjectApply(MakeLazyRawPreview(editor->GetRawWorkspaceState().sources.front(), root)),
            "Could not begin lazy RAW preview load");
        wait([&] { return !editor->IsDeferredLoadedProjectApplyActive(); }, "Lazy RAW first-render timed out");
        CheckStage(!editor->HasDeferredLoadedProjectApplyFailed(), editor->GetDeferredLoadedProjectStatusText());
        CheckStage(editor->TryGetActiveRawWorkspacePresentationTexture(texture, outputWidth, outputHeight),
            "Lazy RAW preview has no first-frame presentation: " + editor->GetActiveRawWorkspacePresentationDiagnosticForValidation());
        std::cout << "PASS deferred lazy RAW preview publishes its first frame\n";
        CheckStage(discoveredCopy, "Browsing the external copy's folder did not discover it");
        std::cout << "PASS Save As gets its own identity and becomes discoverable in its chosen folder\n";
        success = true;
    } catch (const std::exception& e) { std::cerr << "Project stage validation failed: " << e.what() << '\n'; }
    editor->RequestWorkerShutdownForAppClose();
    BracketingRawToolValidationAccess::FinishDraftWorkspace(*editor);
    Async::TaskSystem::Get().Shutdown();
    editor.reset();
    ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return success;
}
}
