#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Async/TaskSystem.h"
#include "Persistence/BracketingProject.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include "BracketingGpuMotionValidation.h"
#include "BracketingRawHandoffValidation.h"
#include "BracketingRenderValidation.h"
#include "BracketingRawToolValidation.h"
#include "SuperResolutionContracts.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace Stack::Validation {
bool ValidateBracketingComparison(const Project::RawProjectSnapshot&,const std::vector<std::filesystem::path>&,const std::filesystem::path&);
bool ValidateBracketingWorkflow(int argc,char** argv,bool superResolution,unsigned scale) {
    if(argc<2) return false;
    const auto check=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    if(!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(640,480,"Bracket workflow validation",nullptr,nullptr);
    if(!window) {glfwTerminate();return false;}
    glfwMakeContextCurrent(window);if(!LoadGLFunctions()) {glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
    ImGui::GetIO().DisplaySize={640,480};unsigned char* fontPixels=nullptr;int fontW=0,fontH=0;ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels,&fontW,&fontH);
    Async::TaskSystem::Get().Initialize();bool success=false;
    auto editor=std::make_unique<EditorModule>();
    try {
        ValidateBracketingGpuMotion();
        const std::filesystem::path root=argv[0];std::filesystem::create_directories(root);
        std::vector<std::filesystem::path> files;for(int i=1;i<argc;++i) files.emplace_back(argv[i]);
        std::string error;
        std::cout<<"Workflow: importing captures\n"<<std::flush;
        const auto draftRoot=root/("bracket-"+Project::GenerateStableUuid());
        auto initialFiles=files;
        const bool testAddition=initialFiles.size()>2;
        if(testAddition)initialFiles.pop_back();
        BracketingRawToolValidationAccess::SetWorkflowSources(*editor,draftRoot,initialFiles);
        const auto inspectUntil=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(!BracketingRawToolValidationAccess::MetadataReady(*editor)&&std::chrono::steady_clock::now()<inspectUntil) {
            ImGui::NewFrame();editor->TickBracketing();ImGui::EndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        check(BracketingRawToolValidationAccess::MetadataReady(*editor),"Draft metadata inspection timed out");
        check(editor->CommitBracketingDraft(false,&error),error);
        std::cout<<"Workflow: activating project\n"<<std::flush;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(!editor->HasDeferredLoadedProjectApplyCoreFinished()&&!editor->HasDeferredLoadedProjectApplyFailed()&&std::chrono::steady_clock::now()<deadline) {
            ImGui::NewFrame();editor->PumpNonRenderingWork(10);ImGui::EndFrame();Async::TaskSystem::Get().PumpMainThreadTasks();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(editor->IsBracketingActive(),"Imported bracket was not activated: "+editor->GetDeferredLoadedProjectStatusText());
        editor->TickBracketing();
        editor->OpenBracketingTool();
        if(superResolution)BracketingRenderValidationAccess::SelectSuperResolution(*editor,scale);
        const auto samples=[](const Raw::RawImageData& raw)->const std::vector<float>& {
            return raw.reconstructedCameraRgb?raw.linearFloatBuffer:*raw.normalizedMosaicBuffer;
        };
        std::cout<<"Workflow: checking hover\n"<<std::flush;
        const auto hasMosaic=[&] {for(const auto& n:editor->BuildGraphSnapshot().nodes) if(n.rawDevelopment.embeddedRawData) return true;return false;};
        {
            Workspace::PresentationScope hover(true);editor->EnterBracketingRaw();
            check(!hasMosaic(),"Selector hover published a bracket");
        }
        const auto wait=[&] {
            // Full-resolution Automatic local analysis on high-megapixel RAW
            // bursts can legitimately exceed five minutes on a portable CPU.
            // This is a validation watchdog, not an editor processing limit.
            const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(20);
            auto nextDiagnostic=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(editor->IsActiveMultiFrameProcessingForQueueBusy()&&std::chrono::steady_clock::now()<until) {
                check(!editor->DidActiveMultiFrameProcessingForQueueFail(&error),error);
                if(std::chrono::steady_clock::now()>=nextDiagnostic) {
                    double progress=0;std::string stage;
                    if(editor->GetActiveBracketingProcessingDiagnostic(progress,stage))
                        std::cout<<"Workflow progress: "<<static_cast<int>(progress*100+.5)<<"% "<<stage<<'\n'<<std::flush;
                    nextDiagnostic=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                }
                editor->TickBracketing();Async::TaskSystem::Get().PumpMainThreadTasks();std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            check(!editor->DidActiveMultiFrameProcessingForQueueFail(&error),error);
            check(!editor->IsActiveMultiFrameProcessingForQueueBusy(),"Bracket job timed out");editor->TickBracketing();
        };
        std::cout<<"Workflow: processing bracket\n"<<std::flush;
        check(editor->CommitBracketingDraft(true,&error),error);wait();
        check(hasMosaic(),"Process did not automatically publish into RAW");
        if(testAddition) {
            const auto submitted=editor->GetActiveRawProjectSnapshot()->sourceSets.front().settings["bracketing"];
            const auto revision=editor->GetActiveRawProjectSnapshot()->hdrInputRevision;
            editor->AddBracketingDraftFiles({files.back()});
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!BracketingRawToolValidationAccess::MetadataReady(*editor)&&std::chrono::steady_clock::now()<until) {
                ImGui::NewFrame();editor->TickBracketing();ImGui::EndFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(editor->CommitBracketingDraft(false,&error),error);
            auto draft=Project::OpenProjectStore(editor->GetCurrentProjectFileName());check(bool(draft),draft.message);
            check(draft.snapshot.sourceSets.front().settings["bracketing"]==submitted&&draft.snapshot.hdrInputRevision==revision,
                "Saving new draft assets changed the submitted recipe or output revision");
            check(draft.snapshot.sourceSets.front().frames.size()==files.size(),"Save did not store newly selected captures");
            check(editor->HasPendingBracketingDraft()&&!editor->IsActiveMultiFrameProcessingForQueueBusy(),
                "Adding and saving a capture started a merge");
            check(editor->CommitBracketingDraft(true,&error),error);wait();
            std::cout<<"Added capture stayed pending through Save and merged only after Process.\n";
        }
        std::cout<<"Workflow: entering RAW\n"<<std::flush;
        editor->EnterBracketingRaw();check(hasMosaic(),"RAW entry did not publish the matching mosaic");
        const auto original=editor->BuildGraphSnapshot();std::vector<float> expected;
        for(const auto& n:original.nodes) if(n.rawDevelopment.embeddedRawData) expected=samples(*n.rawDevelopment.embeddedRawData);
        for(const auto& n:original.nodes) if(n.rawDevelopment.embeddedRawData) {
            nlohmann::json handoffReport;
            if(superResolution)SrContracts::RawHandoff(n.rawDevelopment.embeddedRawData,handoffReport);
            else InspectBracketRawHandoff(n.rawDevelopment.embeddedRawData,handoffReport);
            std::ofstream(root/"raw-handoff.json")<<handoffReport.dump(2);
        }
        std::cout<<"Workflow: saving project\n"<<std::flush;
        BracketingRawToolValidationAccess::ResultStack(*editor);
        BracketingRenderValidationAccess::Run(*editor,window);
        BracketingRawToolValidationAccess::InteractiveCurve(*editor);
        BracketingRenderValidationAccess::WaitForIdle(*editor);
        BracketingRawToolValidationAccess::UpdatedNativeRegion(*editor);
        BracketingRawToolValidationAccess::PendingPersistence(*editor);
        check(editor->SaveActiveMultiFrameRawProject(&error),error);
        check(editor->SaveActiveMultiFrameRawProjectAs(root/"portable.stack",Project::ProjectStorageKind::PortableFile,&error),error);
        auto opened=Project::OpenProjectStore(root/"portable.stack");check(static_cast<bool>(opened),opened.message);
        const auto savedSettings=opened.snapshot.sourceSets.front().settings;
        EditorLoadedProjectData loaded;loaded.sourceState=ProjectSourceState::LazyAsset;
        loaded.projectStore=opened.store;loaded.rawProjectSnapshot=std::make_shared<Project::RawProjectSnapshot>(opened.snapshot);
        loaded.pipelineData=opened.snapshot.pipelineData;loaded.rawWorkspaceData=opened.snapshot.rawWorkspaceData;
        loaded.projectKind=StackBinaryFormat::kRawProjectKind;loaded.projectName=opened.snapshot.projectName;loaded.projectFileName=(root/"portable.stack").string();
        std::cout<<"Workflow: reopening for Queue\n"<<std::flush;
        check(editor->ApplyLoadedProject(loaded),"Could not reopen portable bracket");
        check(editor->StartActiveMultiFrameProcessingForQueue(&error),error);wait();
        check(hasMosaic(),"Queue did not publish reopened bracket");
        const auto graph=editor->BuildGraphSnapshot();bool identical=false;
        for(const auto& n:graph.nodes) if(n.rawDevelopment.embeddedRawData) {
            const auto& actual=samples(*n.rawDevelopment.embeddedRawData);
            identical=expected==actual;
            if(!identical) {
                double maxDifference=0,meanDifference=0;std::size_t changed=0;
                bool withinBackendTolerance=actual.size()==expected.size();
                for(std::size_t i=0;i<std::min(expected.size(),actual.size());++i) {
                    const double d=std::abs(static_cast<double>(expected[i])-actual[i]);
                    maxDifference=std::max(maxDifference,d);meanDifference+=d;changed+=d!=0;
                    withinBackendTolerance&=std::isfinite(actual[i])&&std::isfinite(expected[i])&&
                        d<=2e-5*std::max(1.,std::abs(static_cast<double>(expected[i])));
                }
                std::cout<<"Reopen comparison: sizes "<<expected.size()<<"/"<<actual.size()<<", changed "<<changed
                    <<", max "<<maxDifference<<", mean "<<meanDifference/std::max<std::size_t>(1,expected.size())<<'\n';
                // SR uses its verified GPU kernel once the RAW render owner exists.
                // Match that kernel's established CPU/GPU tolerance; Standard remains exact.
                identical=superResolution&&withinBackendTolerance;
            }
        }
        check(identical,"Reopened Queue result differs from the authored mosaic");
        check(editor->GetActiveRawProjectSnapshot()->sourceSets.front().settings==savedSettings,"RAW handoff changed saved RAW settings");
        std::cout<<"Workflow: exporting RAW development\n"<<std::flush;
        RenderPipeline renderer;renderer.Initialize();renderer.ExecuteGraph(graph);
        int w=0,h=0;const auto rgba=renderer.GetOutputPixels(w,h);
        check(w>0&&h>0&&!rgba.empty(),"RAW development/export produced no pixels: "+renderer.GetLastGraphExecutionStats().lastSpecializedFailure);
        check(stbi_write_png((root/"export.png").string().c_str(),w,h,4,rgba.data(),w*4)!=0,"Could not write bracket export");
        Raw::Bracketing::BracketingRecipe authored;
        check(Raw::Bracketing::Deserialize(savedSettings["bracketing"],authored,error),error);
        if(authored.groups.size()>1) for(const auto selected:{std::size_t(0),authored.groups.size()-1}) {
            authored.automatic=false;
            for(auto& knot:authored.knots) {
                knot.share.assign(authored.groups.size(),0);knot.share[selected]=1;
                knot.left=knot.right=knot.share;
            }
            loaded.rawProjectSnapshot=std::make_shared<Project::RawProjectSnapshot>(opened.snapshot);
            loaded.rawProjectSnapshot->hdrInputRevision+=selected+1;
            loaded.rawProjectSnapshot->sourceSets.front().settings["bracketing"]=Raw::Bracketing::Serialize(authored);
            check(editor->ApplyLoadedProject(loaded),"Could not apply edited bracket");
            check(editor->StartBracketingProcessing(false,&error),error);wait();editor->EnterBracketingRaw();
            const auto editedGraph=editor->BuildGraphSnapshot();
            double difference=0,maxDifference=0;std::size_t changed=0;bool found=false;
            for(const auto& node:editedGraph.nodes) if(node.rawDevelopment.embeddedRawData) {
                found=true;const auto& editedSamples=samples(*node.rawDevelopment.embeddedRawData);
                check(editedSamples.size()==expected.size(),"Edited RAW mosaic size changed");
                check(node.rawProjectSourceSet.inputRevision==loaded.rawProjectSnapshot->hdrInputRevision,"RAW published an older bracket revision");
                for(std::size_t i=0;i<editedSamples.size();++i) {
                    const double d=std::abs(static_cast<double>(editedSamples[i])-expected[i]);difference+=d;maxDifference=std::max(maxDifference,d);changed+=d>1e-6;
                }
            }
            check(found,"Edited bracket was not handed to RAW");
            renderer.ExecuteGraph(editedGraph);int ew=0,eh=0;const auto editedPixels=renderer.GetOutputPixels(ew,eh);
            check(ew==w&&eh==h&&editedPixels.size()==rgba.size(),"Edited RAW export failed");
            std::size_t displayChanged=0;for(std::size_t i=0;i<rgba.size();++i)displayChanged+=rgba[i]!=editedPixels[i];
            check(stbi_write_png((root/("group-"+std::to_string(selected+1)+"-requested.png")).string().c_str(),ew,eh,4,editedPixels.data(),ew*4)!=0,"Could not write edited export");
            std::cout<<"100% group "<<selected+1<<": changed mosaic samples "<<changed<<", mean absolute difference "<<difference/expected.size()
                <<", maximum difference "<<maxDifference<<", changed display channels "<<displayChanged<<'\n'<<std::flush;
        }
        renderer.Shutdown();
        if(!superResolution)check(ValidateBracketingComparison(opened.snapshot,files,root),"Native comparison failed");
        std::cout<<"Bracket import, hover isolation, committed RAW handoff, portable reopen, Queue processing, and RAW export passed.\n";success=true;
    } catch(const std::exception& e) {std::cerr<<"Bracket workflow failed: "<<e.what()<<'\n';}
    editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();editor.reset();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
