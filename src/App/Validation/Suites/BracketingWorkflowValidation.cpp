#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "Async/TaskSystem.h"
#include "Persistence/BracketingProject.h"
#include "Persistence/BracketingResultStore.h"
#include "Project/BracketingJobRunner.h"
#include <future>
#include <cstring>
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
namespace {
// Only used with an exclusively created fixture directory containing our own copies.
void ValidateClosetBracketStorage(std::unique_ptr<EditorModule>& editor,const std::filesystem::path& root,
    const std::vector<std::filesystem::path>& captures,const std::vector<float>& expected,
    const nlohmann::json& developedBaseline) {
    namespace fs=std::filesystem;
    const auto check=[](bool ok,const std::string& reason) { if (!ok) throw std::runtime_error(reason); };
    std::string error;
    check(editor->SaveActiveMultiFrameRawProject(&error),error);
    const fs::path project=editor->GetCurrentProjectFileName();
    auto working=Project::OpenProjectStore(project);
    check(bool(working),working.message);
    const auto setId=working.snapshot.sourceSets.at(0).sourceSetId;
    const auto& saved=working.snapshot.sourceSets.front().settings.at("bracketingResult");
    auto master=working.store->ResolveAssetFile(saved.at("masterAssetId").get<std::string>());
    check(fs::equivalent(master.parent_path(),captures.front().parent_path()),"Master is outside the capture folder");
    const auto exact=[&](const auto& result) {
        if (!result.raw || !result.raw->normalizedMosaicBuffer) return false;
        const auto& data=*result.raw->normalizedMosaicBuffer;
        return data.size()==expected.size() && std::memcmp(data.data(),expected.data(),data.size()*sizeof(float))==0;
    };
    const auto restore=[&](const auto& store, bool verifyDevelopment = false) {
        Raw::Bracketing::BracketingResult restored;
        check(Project::RestoreBracketingResult(store.store,store.snapshot,setId,restored,error),error);
        check(exact(restored),"Master reopened with different engine samples");
        if (verifyDevelopment) {
            nlohmann::json reopened;
            InspectBracketRawHandoff(restored.raw, reopened, true);
            check(reopened.at("rawHandoff") == developedBaseline,
                "Native RAW development changed dimensions or rendered pixel values after reopening");
        }
    };
    restore(working, true);
    const auto portablePath=root.parent_path()/(root.filename().string()+"-portable.stack");
    check(editor->SaveActiveMultiFrameRawProjectAs(portablePath,Project::ProjectStorageKind::PortableFile,&error),error);
    // Exercise reopening after the authoring tab closes. Retaining its engine
    // session here would charge two unrelated full-resolution jobs to the host.
    editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();
    editor.reset();
    Async::TaskSystem::Get().Initialize();
    const auto portable=Project::OpenProjectStore(portablePath);
    check(bool(portable),portable.message);
    for (const auto& file:captures) fs::rename(file,file.wstring()+L".offline");
    restore(working);
    const auto renamed=master.parent_path()/("Renamed master"+master.extension().string());
    fs::rename(master,renamed);
    const auto pausedProject=project.wstring()+L".offline";
    fs::rename(project,pausedProject);
    auto scan=RawWorkspace::ScanWorkspace(root);
    check(scan.success,scan.errorMessage);
    check(scan.sources.empty(),"The marked result was classified as an original");
    check(RawWorkspace::DiscoverWorkspaceProjects(scan.layout,scan.sources,scan.sourceSetProjects),"Result discovery failed");
    check(std::any_of(scan.sourceSetProjects.begin(),scan.sourceSetProjects.end(),
        [&](const auto& card){return card.standaloneMaster && fs::equivalent(card.absolutePath,renamed);}),
        "The isolated result did not appear in Brackets");
    fs::rename(pausedProject,project);
    restore(working);
    fs::rename(renamed,renamed.wstring()+L".offline");
    restore(portable, true);
    Project::BracketingJobRequest job;
    job.store=working.store;job.snapshot=working.snapshot;job.setId=setId;job.cacheRoot=root/"Closet"/"Cache";
    check(Raw::Bracketing::Deserialize(working.snapshot.sourceSets.front().settings.at("bracketing"),job.recipe,error),error);
    auto missing=Project::OpenOrRegenerateBracket(job);
    check(!missing.raw && !missing.message.empty(),"Missing captures and result did not report unavailable media");
    const auto relocation=root/"Relocated";
    fs::create_directory(relocation);
    for (const auto& file:captures) fs::rename(file.wstring()+L".offline",relocation/("Moved "+file.filename().string()));
    std::size_t count=0;
    check(Project::RelinkProjectSources(project,relocation,count,error),error);
    check(count==captures.size(),"One Locate folder did not reconnect all captures");
    working=Project::OpenProjectStore(project);
    check(bool(working),working.message);
    job.store=working.store;job.snapshot=working.snapshot;
    auto other=std::async(std::launch::async,[&]{return Project::OpenOrRegenerateBracket(job);});
    auto recovered=Project::OpenOrRegenerateBracket(job);
    auto shared=other.get();
    check(exact(recovered),"Regenerated samples changed or regeneration failed: "+recovered.message);
    check(recovered.raw==shared.raw,"Simultaneous opens did not share a single recovery job");
    check(Project::SaveBracketingResult(working.store,working.snapshot,setId,recovered,{},error),error);
    restore(working);
    std::size_t masters=0;
    for (const auto& file:fs::directory_iterator(relocation))
        if (file.path().extension()==".dng") ++masters;
    check(masters==1,"Regeneration published an incorrect number of masters");
    std::cout<<"Closet bracket passed. Matching native development, exact working and portable reopen, result-only discovery, missing media, "
        "batch relink, shared regeneration and one replacement master. Samples: "<<expected.size()<<'\n';
}
} // namespace
bool ValidateBracketingWorkflow(int argc,char** argv,bool superResolution,unsigned scale,bool storageOnly) {
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
        if (!storageOnly) ValidateBracketingGpuMotion();
        const std::filesystem::path root=argv[0];std::filesystem::create_directories(root);
        std::vector<std::filesystem::path> files;for(int i=1;i<argc;++i) files.emplace_back(argv[i]);
        std::string error;
        std::cout<<"Workflow: importing captures\n"<<std::flush;
        const auto draftRoot=root/("bracket-"+Project::GenerateStableUuid());
        check(std::filesystem::create_directory(draftRoot),"Could not create exclusive bracket fixture");
        if (storageOnly) {
            auto folder=draftRoot/"Photos";
            std::filesystem::create_directory(folder);
            for (auto& file:files) {
                auto copy=folder/file.filename();
                std::filesystem::copy_file(file,copy);
                file=copy;
            }
        }
        check(Stack::RawWorkspace::EnsureManagedFolders(draftRoot,&error),error);
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
        const auto inspected=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(!BracketingRawToolValidationAccess::MetadataReady(*editor) && std::chrono::steady_clock::now()<inspected) {
            ImGui::NewFrame();editor->TickBracketing();ImGui::EndFrame();
            Async::TaskSystem::Get().PumpMainThreadTasks();std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        check(BracketingRawToolValidationAccess::MetadataReady(*editor),"Reopened capture metadata inspection timed out");
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
        std::vector<float> expected;
        nlohmann::json developedBaseline;
        {
            const auto original=editor->BuildGraphSnapshot();
            for(const auto& n:original.nodes) if(n.rawDevelopment.embeddedRawData) expected=samples(*n.rawDevelopment.embeddedRawData);
            for(const auto& n:original.nodes) if(n.rawDevelopment.embeddedRawData) {
                nlohmann::json handoffReport;
                if(superResolution)SrContracts::RawHandoff(n.rawDevelopment.embeddedRawData,handoffReport);
                else InspectBracketRawHandoff(n.rawDevelopment.embeddedRawData,handoffReport,storageOnly);
                if (storageOnly) developedBaseline = handoffReport.at("rawHandoff");
                std::ofstream(root/"raw-handoff.json")<<handoffReport.dump(2);
            }
        }
        std::cout<<"Workflow: saving project\n"<<std::flush;
        if (storageOnly) {
            ValidateClosetBracketStorage(editor,draftRoot,files,expected,developedBaseline);
            success=true;
        } else {
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
        }
    } catch(const std::exception& e) {std::cerr<<"Bracket workflow failed: "<<e.what()<<'\n';}
    if (editor) editor->RequestWorkerShutdownForAppClose();
    Async::TaskSystem::Get().Shutdown();editor.reset();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
