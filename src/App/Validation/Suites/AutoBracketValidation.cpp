#include "Editor/AutoBracket/AutoBracketCoordinator.h"
#include "Editor/AutoBracket/AutoBracketPresentation.h"
#include "Editor/EditorModule.h"
#include "Editor/RawRenderService.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace Stack::Validation {
namespace {
void Check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
template<class T> bool Same(const std::shared_ptr<const std::vector<T>>& a,
    const std::shared_ptr<const std::vector<T>>& b) {
    return bool(a)==bool(b)&&(!a||*a==*b);
}
void CheckResult(const Raw::Bracketing::BracketingResult& expected,
    const Raw::Bracketing::BracketingResult& restored) {
    Check(expected.raw&&restored.raw,"Missing restored pixels");
    const auto& a=*expected.raw;const auto& b=*restored.raw;
    Check(expected.identity==restored.identity&&a.contentIdentity==b.contentIdentity&&
        a.contentIdentityHash==b.contentIdentityHash&&a.normalizedMosaicContentHash==b.normalizedMosaicContentHash&&
        a.normalizedMosaicInputContract==b.normalizedMosaicInputContract&&
        a.reconstructedCameraRgb==b.reconstructedCameraRgb,"Restored content identity changed");
    Check(Same(a.normalizedMosaicBuffer,b.normalizedMosaicBuffer)&&a.linearFloatBuffer==b.linearFloatBuffer,
        "Restored pixels differ");
    Check(EditorNodeGraph::SerializeRawMetadata(a.metadata)==EditorNodeGraph::SerializeRawMetadata(b.metadata),
        "Restored RAW metadata differs");
    Check(a.multiFrameMeasurementSidecars&&b.multiFrameMeasurementSidecars,"Missing measurement sidecars");
    const auto& x=*a.multiFrameMeasurementSidecars;const auto& y=*b.multiFrameMeasurementSidecars;
    Check(Same(x.variance,y.variance)&&Same(x.fusionUncertaintyVariance,y.fusionUncertaintyVariance)&&
        Same(x.effectiveSupport,y.effectiveSupport)&&Same(x.validity,y.validity)&&Same(x.clipping,y.clipping)&&
        Same(x.localRejection,y.localRejection)&&Same(x.fallbackReason,y.fallbackReason)&&
        x.originalFrameIds==y.originalFrameIds&&x.evidenceIdentitySha256==y.evidenceIdentitySha256,
        "Restored measurement sidecars differ");
    Check(expected.preview.resultRgb==restored.preview.resultRgb&&
        expected.preview.sourceRgb==restored.preview.sourceRgb&&expected.preview.contributions==restored.preview.contributions&&
        expected.preview.requested==restored.preview.requested&&expected.preview.guideEv==restored.preview.guideEv&&
        expected.preview.diagnostics==restored.preview.diagnostics,"Restored preview differs");
    Check(expected.preview.samples.size()==restored.preview.samples.size(),"Restored preview observations are missing");
    for(std::size_t i=0;i<expected.preview.samples.size();++i) {
        const auto& p=expected.preview.samples[i];const auto& q=restored.preview.samples[i];
        const float before[]={p.value,p.variance,p.headroom,p.support,p.fallback,p.exposure,p.measurementVariance,p.uncertaintyVariance};
        const float after[]={q.value,q.variance,q.headroom,q.support,q.fallback,q.exposure,q.measurementVariance,q.uncertaintyVariance};
        Check(std::memcmp(before,after,sizeof(before))==0&&p.finite==q.finite&&p.clipped==q.clipped&&
            p.localRejected==q.localRejected&&p.fixedReference==q.fixedReference,"Restored preview observations differ");
    }
}
}

bool ValidateAutoBracketReopen(const std::filesystem::path& root) {
    try {
        AutoBracket::Queue queue;std::string error;
        Check(AutoBracket::LoadQueue(root,queue,error),error);
        std::size_t completed=0;
        for(const auto& item:queue.items)if(item.state==AutoBracket::State::Completed) {
            auto project=Project::OpenProjectStore(item.projectPath);Check(bool(project),project.message);
            Check(project.snapshot.projectId==item.projectId,"Restart changed a project association");
            Check(project.store->Verify(project.snapshot),"Saved project assets failed verification after restart");
            Raw::Bracketing::BracketingResult result;
            Check(Project::RestoreBracketingResult(project.store,project.snapshot,project.snapshot.activeSourceSetId,result,error),error);
            Check(result.raw&&result.raw->multiFrameMeasurementSidecars&&!result.analysis&&
                !project.snapshot.coverThumbnailBytes.empty(),"Restart lost the saved result, measurements or cover");
            ++completed;
        }
        Check(completed>=2,"Restart did not recover separate completed brackets");
        std::cout<<"Fresh-process reopen: "<<completed<<" independent bracket results verified without merging.\n";
        return true;
    }catch(const std::exception& error){std::cerr<<"Automatic bracket reopen failed: "<<error.what()<<'\n';return false;}
}

// Exercises production services with existing RAW fixtures and public editor APIs.
bool ValidateAutoBracket(int argc,char** argv) {
    if(argc!=5&&argc!=6)return false;
    using namespace AutoBracket;
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(640,480,"Automatic bracket validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().DisplaySize={640,480};
    unsigned char* fonts=nullptr;int width=0,height=0;ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fonts,&width,&height);
    Async::TaskSystem::Get().Initialize();
    auto& render=EditorRendering::RawRenderService::Get();const auto client=render.Acquire(window);
    const Raw::OpenGlTaskExecutor executor=[&](Raw::OpenGlTask task,std::string& error) {
        return render.ExecuteOpenGlTaskBlocking(std::move(task),error);
    };
    auto editor=std::make_unique<EditorModule>();
    AutoBracketCoordinator queue;
    bool success=false;
    try {
        const auto root=std::filesystem::absolute(argv[0])/Project::GenerateStableUuid();
        std::filesystem::create_directories(root/"first");std::filesystem::create_directories(root/"second");
        for(int i=0;i<4;++i)std::filesystem::copy_file(argv[i+1],root/(i<2?"first":"second")/std::filesystem::path(argv[i+1]).filename());
        const auto scan=RawWorkspace::ScanWorkspace(root);
        Check(scan.success&&scan.sources.size()==4,"Root scan did not collect both subfolders");
        std::map<std::string,RawWorkspace::RawGallerySimilarityStack> grouped;
        for(const auto& source:scan.sources)grouped[source.parentFolderKey].sourceKeys.push_back(source.relativePathKey);
        std::vector<RawWorkspace::RawGallerySimilarityStack> stacks;
        for(auto& [folder,stack]:grouped){stack.folderKey=folder;stacks.push_back(stack);}
        stacks.front().sourceKeys.push_back(stacks.front().sourceKeys.front());
        const auto candidates=CollectCandidates(scan.sources,stacks);
        Check(candidates.size()==2&&candidates.front().sources.size()==2,"Distinct root-wide capture collection failed");
        auto reversed=candidates.front().sources;std::reverse(reversed.begin(),reversed.end());
        Check(SourceIdentity(reversed)==candidates.front().identity,"Source identity depends on order");
        auto corrected=stacks;corrected.front().sourceKeys.resize(1);
        Check(CollectCandidates(scan.sources,corrected).size()==1,"Manual grouping correction was ignored");

        std::string error;
        Check(editor->CreateMultiFrameRawProject(root/"foreground",Project::ProjectStorageKind::DirectoryBundle,
            "Foreground edits","Captures",Project::MultiFrameOperationIntent::RawCaptureSet,
            {argv[1],argv[2]},0,&error),error);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
        while(!editor->HasDeferredLoadedProjectApplyCoreFinished()&&!editor->HasDeferredLoadedProjectApplyFailed()&&
            std::chrono::steady_clock::now()<deadline) {
            ImGui::NewFrame();editor->PumpNonRenderingWork(10);ImGui::EndFrame();
            Async::TaskSystem::Get().PumpMainThreadTasks();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(editor->HasDeferredLoadedProjectApplyCoreFinished(),editor->GetDeferredLoadedProjectStatusText());
        auto& graph=editor->GetNodeGraph();Check(!graph.GetNodes().empty(),"Foreground graph was not loaded");
        graph.EditNodes().front().title="Unsaved foreground edit";editor->MarkDirty();
        const auto filterId=graph.AddFrequencyFilterNode(EditorNodeGraph::FrequencyFilterMode::BandStop,{240,240})->id;
        Check(editor->ExtractFrequencyResponseNode(filterId,&error),error);
        Check(editor->CanUndoFrequencyGraphAction(),"Foreground undo history was not populated");
        graph.SelectNode(graph.GetNodes().front().id);
        const auto foreground=Project::SerializeRawProjectSnapshot(*editor->GetActiveRawProjectSnapshot());
        const auto foregroundGraph=EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),graph);
        const auto selection=graph.GetSelectedNodeIds();const bool undo=editor->CanUndoFrequencyGraphAction();
        const auto structureRevision=graph.GetStructureRevision();
        const auto checkForeground=[&] {
            Check(editor->IsDirty()&&Project::SerializeRawProjectSnapshot(*editor->GetActiveRawProjectSnapshot())==foreground&&
                EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),graph)==foregroundGraph&&
                graph.GetSelectedNodeIds()==selection&&editor->CanUndoFrequencyGraphAction()==undo&&
                graph.GetStructureRevision()==structureRevision,
                "Background processing changed the foreground document, edits, selection or undo state");
        };
        Check(queue.Enabled(),"Automatic processing is not enabled by default");
        Check(queue.SetRoot(root,0),"Could not select validation root");queue.UpdateCandidates(candidates,{});
        Check(queue.CanStartWork(0,true,false),"Automatic queue did not become eligible immediately");
        queue.RunNow();queue.Tick(59,false,false,executor);Check(!queue.HasWork(),"Run now bypassed readiness");
        queue.Tick(59,true,true,executor);Check(!queue.HasWork(),"Run now competed with foreground work");
        queue.Tick(59,true,false,executor);Check(queue.HasWork(),"Run now did not start");
        auto first=queue.Current();
        Queue reserved;Check(LoadQueue(root,reserved,error)&&reserved.items.front().projectId==first->item.projectId,
            "Project identity was not reserved before work");
        Presentation presentation;presentation.Begin(first,59);
        queue.SetPaused(true);bool continued=false;
        Check(queue.RequestForeground("continue editing",[&]{continued=true;checkForeground();}),"Takeover was not held");
        queue.ResolveForeground(false);Check(!continued,"Finish takeover ran before the worker drained");
        const auto finish=[&](double now) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(5);
            while(queue.HasWork()&&std::chrono::steady_clock::now()<until) {
                queue.Tick(now,true,false,executor);Async::TaskSystem::Get().PumpMainThreadTasks();
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            Check(!queue.HasWork(),"Automatic bracket timed out");
            auto completed=queue.ConsumeCompleted();Check(bool(completed),"Missing job completion");return completed;
        };
        first=finish(60);Check(first->outcome==State::Completed,first->status);Check(continued,"Finish takeover was lost");
        checkForeground();presentation.Complete(first,60);
        Check(presentation.animation.active&&presentation.texture!=0,"Saved completion presentation is unavailable");
        ImGui::NewFrame();DrawPresentation(presentation,queue);ImGui::EndFrame();presentation.Tick(120);
        Check(!presentation.animation.active,"Completion animation did not finish");
        Check(first->mailbox->Read()->progress.stage==Raw::Bracketing::ProcessingStage::Completed,"Completion preceded durable saving");
        queue.Tick(200,true,false,executor);Check(!queue.HasWork(),"Pause started another bracket");
        queue.Shutdown();Check(queue.SetRoot({},201),"Could not leave root");Check(queue.SetRoot(root,201),"Could not restore queue");
        queue.UpdateCandidates(candidates,{});Check(queue.GetQueue().paused,"Pause did not survive reload");
        queue.SetPaused(false);queue.NoteActivity(201);queue.Tick(260,true,false,executor);
        Check(queue.HasWork(),"Resuming did not start the ready queue");
        const auto canceledId=queue.Current()->item.projectId;continued=false;
        Check(queue.RequestForeground("continue editing",[&]{continued=true;checkForeground();}),"Cancel takeover was not held");
        queue.ResolveForeground(true);auto canceled=finish(262);
        Check(continued&&canceled->outcome==State::Pending,"Canceled bracket did not remain resumable");
        Check(queue.GetQueue().items.front().state==State::Completed,"Cancellation reset completed work");
        checkForeground();queue.RunNow();queue.Tick(263,true,false,executor);
        Check(queue.HasWork()&&queue.Current()->item.projectId==canceledId,"Cancellation created a duplicate project");
        queue.NoteActivity(264);auto second=finish(265);Check(second->outcome==State::Completed,second->status);checkForeground();
        queue.Tick(326,true,false,executor);Check(!queue.HasWork(),"Completed stacks were processed twice");
        Check(editor->UndoFrequencyGraphAction()&&editor->CanRedoFrequencyGraphAction(),"Foreground undo history could not be used after takeover");
        Check(editor->RedoFrequencyGraphAction(),"Foreground redo history could not be used after takeover");

        for(const auto& completed:{first,second}) {
            auto reopened=Project::OpenProjectStore(completed->item.projectPath);Check(bool(reopened),reopened.message);
            Check(!reopened.snapshot.coverThumbnailBytes.empty(),"Saved bracket has no cover");
            Raw::Bracketing::BracketingResult restored;
            Check(Project::RestoreBracketingResult(reopened.store,reopened.snapshot,reopened.snapshot.activeSourceSetId,restored,error),error);
            CheckResult(*completed->result,restored);Check(!restored.analysis,"Reopening unexpectedly rebuilt preparation");
            Project::BracketingJobRequest request;request.store=reopened.store;request.snapshot=reopened.snapshot;
            request.setId=reopened.snapshot.activeSourceSetId;request.cacheRoot=root/"cache";
            Raw::Bracketing::Deserialize(reopened.snapshot.sourceSets.front().settings.at("bracketing"),request.recipe,error);
            bool restoredOnly=false;request.didRestoreResult=[&]{restoredOnly=true;};
            const auto loaded=Project::RunBracketingJob(request);Check(restoredOnly,"Open reran the merge");CheckResult(restored,loaded);
            if(completed==first) {
                request.preparationOnly=true;request.executeOpenGlTask=executor;
                const auto prepared=Project::RunBracketingJob(request);
                Check(prepared.status==Raw::Bracketing::BracketingResult::Status::Completed&&prepared.analysis&&
                    !prepared.raw,"Inspection preparation generated another result");
                CheckResult(restored,loaded);
            }
        }
        for(const auto kind:{Project::ProjectStorageKind::DirectoryBundle,Project::ProjectStorageKind::PortableFile}) {
            auto copied=Project::ConvertProjectStore(first->project.store,first->project.snapshot,
                root/(kind==Project::ProjectStorageKind::DirectoryBundle?"copied":"portable.stack"),kind);
            Check(bool(copied),copied.message);Check(copied.store->Verify(copied.snapshot),"Copied assets failed verification");
            Raw::Bracketing::BracketingResult restored;
            Check(Project::RestoreBracketingResult(copied.store,copied.snapshot,copied.snapshot.activeSourceSetId,restored,error),error);
            CheckResult(*first->result,restored);
        }
        Queue independent;independent.root=first->item.projectPath.parent_path();
        Reconcile(independent,candidates,{{candidates.front().identity,{first->item.projectId,first->item.projectPath}}});
        Check(independent.items.front().state==State::Completed,"An existing manual bracket was not recognized");
        independent.items.front().state=State::Attention;independent.items.front().error="Capture needs review";
        independent.items.back().state=State::Excluded;
        Check(SaveQueue(independent,error),error);Queue loaded;Check(LoadQueue(independent.root,loaded,error),error);
        Reconcile(loaded,candidates,{});
        Check(loaded.items.front().state==State::Attention&&loaded.items.back().state==State::Excluded,
            "Attention or explicit exclusion was lost");
        if(argc==6) {
            queue.Shutdown();Check(queue.SetRoot(root/"incompatible-captures",400),"Could not select failure-check root");
            Candidate incompatible;incompatible.name="Captures needing review";
            for(const auto path:{argv[1],argv[5]}) {
                const auto identity=RawEvidence::ComputeSourceIdentity(path);
                Check(identity.valid,"Could not read the supplied incompatible fixture");
                incompatible.sources.push_back({path,identity.sha256,identity.byteSize});
            }
            incompatible.identity=SourceIdentity(incompatible.sources);
            queue.UpdateCandidates({incompatible,candidates.front()},{});queue.RunNow();queue.Tick(400,true,false,executor);
            auto failed=finish(401);
            Check((failed->outcome==State::Attention||failed->outcome==State::Failed)&&!failed->storageFailure&&
                !failed->status.empty(),"Incompatible captures did not get an individual visible failure");
            queue.Tick(402,true,false,executor);Check(queue.HasWork(),"An individual failure stopped the next bracket");
            auto following=finish(403);Check(following->outcome==State::Completed,following->status);
            queue.Tick(500,true,false,executor);Check(!queue.HasWork(),"The failed bracket retried automatically");
            std::cout<<"Individual failure isolation and no automatic retry: passed\n";
        }
        std::ofstream(root/"validation.txt")<<"PASS readiness, inactivity, Run now, root collection, manual grouping, duplicate prevention, independent saves, restart, exact pixels and sidecars, takeover choices, foreground isolation, cancellation recovery, Pause, copy, portable save and completion presentation.\n";
        std::cout<<"Automatic bracket validation passed: "<<root.string()<<'\n';success=true;
    }catch(const std::exception& error){std::cerr<<"Automatic bracket validation failed: "<<error.what()<<'\n';}
    queue.Shutdown();editor->RequestWorkerShutdownForAppClose();Async::TaskSystem::Get().Shutdown();editor.reset();
    render.Release(client);ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
