#include "Editor/EditorModule.h"
#include "Async/TaskSystem.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/LoadedProjectData.h"
#include "Raw/Bracketing/Recipe.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Renderer/RenderPipeline.h"
#include "ThirdParty/stb_image_write.h"
#include <chrono>
#include <thread>

namespace Stack::Validation {
// Exercise normal project operations against copies of a completed real RAW
// panorama. No test-only access to editor internals or source modifications.
void ValidatePanoramaSessions(GLFWwindow* window,const std::filesystem::path& path,
    const std::filesystem::path& output,nlohmann::json& report) {
    const auto check=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().DisplaySize={640,480};
    unsigned char* fontPixels;int fontWidth,fontHeight;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels,&fontWidth,&fontHeight);
    Async::TaskSystem::Get().Initialize();
    auto a=std::make_unique<EditorModule>(),b=std::make_unique<EditorModule>();
    const auto finish=[&]{
        for(auto* editor:{a.get(),b.get()})editor->RequestWorkerShutdownForAppClose();
        Async::TaskSystem::Get().Shutdown();b.reset();a.reset();ImGui::DestroyContext();
    };
    try {
        auto original=Project::OpenProjectStore(path);check(bool(original),original.message);
        for(auto* editor:{a.get(),b.get()}) {
            editor->Initialize(window,nullptr,false);editor->SetWorkspaceAppStatePersistenceEnabled(false);
            const auto copyPath=output/(editor==a.get()?"tab-a.stack":"tab-b.stack");
            auto copy=Project::ConvertProjectStore(original.store,original.snapshot,copyPath,
                Project::ProjectStorageKind::DirectoryBundle,
                [](const auto&,const auto&,auto& snapshot,std::string& error){
                    snapshot.projectId=Project::GenerateStableUuid();
                    Raw::Bracketing::BracketingRecipe recipe;
                    auto& settings=snapshot.sourceSets.front().settings;
                    if(!Raw::Bracketing::Deserialize(settings.at("bracketing"),recipe,error))return false;
                    // A new authored projection requests actual stitching,
                    // rather than merely restoring the previous cached result.
                    recipe.panorama.projection=Raw::Bracketing::PanoramaProjection::Perspective;
                    settings["bracketing"]=Raw::Bracketing::Serialize(recipe);++snapshot.hdrInputRevision;
                    // Match the application's managed bracket-result graph.
                    EditorNodeGraph::Graph graph;EditorNodeGraph::MultiFrameHdrPayload payload;
                    payload.sourceSetId=snapshot.activeSourceSetId;payload.radiometricAnchorFrameId=recipe.originFrameId;
                    const int source=graph.AddMultiFrameHdrNode(payload,{0,0})->id;
                    const int grade=graph.AddLayerNode(LayerType::ColorGrade,0,{200,0})->id;
                    const int target=graph.AddOutputNode({400,0},true)->id;
                    if(!graph.TryConnectSockets(source,"imageOut",grade,"imageIn",&error)||
                       !graph.TryConnectSockets(grade,"imageOut",target,"imageIn",&error))return false;
                    snapshot.pipelineData=EditorNodeGraph::SerializeGraphPayload(EditorNodeGraph::ExtractLayerArray(snapshot.pipelineData),graph);
                    return true;
                });
            check(bool(copy),copy.message);
            EditorLoadedProjectData loaded;loaded.sourceState=ProjectSourceState::LazyAsset;
            loaded.projectStore=copy.store;loaded.rawProjectSnapshot=std::make_shared<Project::RawProjectSnapshot>(copy.snapshot);
            loaded.pipelineData=copy.snapshot.pipelineData;loaded.rawWorkspaceData=copy.snapshot.rawWorkspaceData;
            loaded.projectKind=StackBinaryFormat::kRawProjectKind;loaded.projectName=copy.snapshot.projectName;loaded.projectFileName=copyPath.string();
            check(editor->ApplyLoadedProject(loaded),"Could not load panorama into its project tab.");
        }
        const auto frame=[&]{
            ImGui::NewFrame();
            for(auto* editor:{a.get(),b.get()}) {
                editor->PumpNonRenderingWork(3,editor==b.get());editor->TickBracketing(editor==b.get());
                editor->UpdateBracketingPresentation(false);
            }
            ImGui::EndFrame();Async::TaskSystem::Get().PumpMainThreadTasks(4);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        const auto wait=[&](auto ready,const char* message){
            const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(3);
            while(!ready()&&std::chrono::steady_clock::now()<until)frame();check(ready(),message);
        };
        // ApplyLoadedProject is synchronous. The deferred-loader completion
        // flag does not apply to this public entry point.
        check(a->GetActiveRawProjectSnapshot()&&b->GetActiveRawProjectSnapshot(),"Panorama tabs did not load.");
        frame();
        check(a->GetProjectDocumentId()!=b->GetProjectDocumentId(),"Panorama tabs share an identity.");
        std::string error;double progress;std::string stage;
        check(a->StartBracketingProcessing(true,&error),error);
        check(b->StartBracketingProcessing(true,&error),error);
        check(a->GetActiveBracketingProcessingDiagnostic(progress,stage)&&b->GetActiveBracketingProcessingDiagnostic(progress,stage),"Panorama jobs did not coexist.");
        a->CancelBracketingPresentation();
        unsigned calls=0;bool saved=false;
        check(b->RequestSaveCurrentProject("Panorama B",[&](bool ok){++calls;saved=ok;}),"Could not save the other panorama tab.");
        wait([&]{return calls&&!a->GetActiveBracketingProcessingDiagnostic(progress,stage)&&!b->GetActiveBracketingProcessingDiagnostic(progress,stage);},"Panorama jobs or save timed out.");
        check(calls==1&&saved,"Panorama save did not complete exactly once.");
        check(!b->DidActiveMultiFrameProcessingForQueueFail(&error),error);
        check(a->GetCurrentProjectName()!=b->GetCurrentProjectName(),"Project names crossed tabs.");
        b->EnterBracketingRaw();frame();
        for(auto* editor:{b.get()}) {
            const auto graph=editor->BuildGraphSnapshot();bool found=false;
            for(const auto& node:graph.nodes)if(node.rawDevelopment.embeddedRawData) {
                found=bool(node.rawDevelopment.embeddedRawData->outputCoverage);
                check(std::abs(node.rawDevelopment.recipe.preToneExposureEv-.3f)<1e-5,"Saved RAW adjustment was lost on editor reopen.");
            }
            check(found,"The completed panorama was not published into its own RAW editor.");
        }
        calls=0;saved=false;
        check(b->RequestSaveCurrentProject("Panorama B",[&](bool ok){++calls;saved=ok;}),"Could not save the completed panorama.");
        wait([&]{return calls!=0;},"Completed panorama save timed out.");check(calls==1&&saved,"Completed panorama save failed.");
        auto reopened=Project::OpenProjectStore(b->GetCurrentProjectFileName());check(bool(reopened),reopened.message);
        Raw::Bracketing::BracketingResult restored;
        check(Project::RestoreBracketingResult(reopened.store,reopened.snapshot,reopened.snapshot.activeSourceSetId,restored,error),error);
        check(restored.raw&&restored.raw->outputCoverage&&restored.panorama,"Saved tab lost its panorama result.");
        EditorLoadedProjectData loaded;loaded.sourceState=ProjectSourceState::LazyAsset;
        loaded.projectStore=reopened.store;loaded.rawProjectSnapshot=std::make_shared<Project::RawProjectSnapshot>(reopened.snapshot);
        loaded.pipelineData=reopened.snapshot.pipelineData;loaded.rawWorkspaceData=reopened.snapshot.rawWorkspaceData;
        loaded.projectKind=StackBinaryFormat::kRawProjectKind;loaded.projectName=reopened.snapshot.projectName;loaded.projectFileName=b->GetCurrentProjectFileName();
        check(b->ApplyLoadedProject(loaded),"Could not reopen the edited panorama in the editor.");
        check(b->StartBracketingProcessing(true,&error),error);
        double restoreProgress=0;
        wait([&]{double p=0;std::string label;const bool active=b->GetActiveBracketingProcessingDiagnostic(p,label);restoreProgress=std::max(restoreProgress,p);return !active;},"Saved panorama restore timed out.");
        check(restoreProgress==0,"Reopening unexpectedly started panorama computation.");
        b->EnterBracketingRaw();frame();const auto graph=b->BuildGraphSnapshot();bool rawEdit=false,graphEdit=false;
        for(const auto& node:graph.nodes) {
            if(node.rawDevelopment.embeddedRawData)rawEdit=bool(node.rawDevelopment.embeddedRawData->outputCoverage)&&std::abs(node.rawDevelopment.recipe.preToneExposureEv-.3f)<1e-5;
            if(node.kind==RenderGraphNodeKind::Layer&&node.layerJson.value("type",std::string())=="ColorGrade")graphEdit=true;
        }
        check(rawEdit&&graphEdit,"Reopening lost the saved RAW or graph edit.");
        RenderPipeline renderer;renderer.Initialize();renderer.SetRawDevelopmentAnalysisEnabled(false);renderer.SetPreviewMaxDimension(0);renderer.ExecuteGraph(graph);
        int width=0,height=0;const auto pixels=renderer.GetOutputPixels(width,height);
        check(width==restored.raw->metadata.visibleWidth&&height==restored.raw->metadata.visibleHeight&&!pixels.empty(),"Reopened editor export changed the panorama dimensions.");
        for(std::size_t p=0;p<restored.raw->outputCoverage->size();++p)
            check(pixels[p*4+3]==std::uint8_t((*restored.raw->outputCoverage)[p]*255),"Reopened editor export lost coverage.");
        check(stbi_write_png((output/"reopened-edited.png").string().c_str(),width,height,4,pixels.data(),width*4)!=0,"Could not export reopened panorama.");
        renderer.Shutdown();report["savedEditorReopenWithoutStitching"]=true;
        report["independentPanoramaTabs"]={{"concurrentStitchingJobs",true},{"independentCancellation",true},{"otherTabSave",true},{"savedRawEdit",true}};
    }catch(...){finish();throw;}
    finish();
}
}
