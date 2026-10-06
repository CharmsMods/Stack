#include "Persistence/BracketingResultStore.h"
#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "Project/BracketingJobRunner.h"
#include "Project/BracketingPreparation.h"
#include "Editor/RawRenderService.h"
#include "BracketingGallery.h"
#include "Raw/Bracketing/SuperResolution/Reconstruction.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "Persistence/BracketingProject.h"
#include "App/AppPaths.h"
#include "App/WorkspacePresentation.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Async/TaskSystem.h"
#include <thread>
#include <utility>

void EditorModule::ResetBracketingForProjectLoad(const std::filesystem::path& projectPath) {
    // A newly created bracket has a draft waiting for its own initial import.
    // Every other open, including the same document after Discard, is a new
    // session whose draft must come from the loaded snapshot.
    if (m_Bracketing && m_Bracketing->newProject &&
        !m_Bracketing->awaitingProject.empty() &&
        m_Bracketing->awaitingProject.lexically_normal() == projectPath.lexically_normal()) return;
    if (m_Bracketing) {
        auto completion = std::exchange(m_Bracketing->saveBeforeCloseCompletion, {});
        if (completion) completion(false);
    }
    m_Bracketing.reset();
    m_HdrAdoptedRawResult.reset();
    m_BracketingPresentedSourceHash = 0;
}

bool EditorModule::IsBracketingActive() const {
    if(!m_Project->snapshot) return false;
    const auto* set=Stack::Project::FindSourceSet(*m_Project->snapshot,m_Project->snapshot->activeSourceSetId);
    return set&&Stack::Project::IsBracketing(*set);
}
bool EditorModule::GetActiveBracketingProcessingDiagnostic(
    double& progress,std::string& stage) const {
    progress=0;stage.clear();
    if(!m_Bracketing||!m_Bracketing->job) return false;
    const auto job=m_Bracketing->job;
    progress=job->progress.load();
    std::lock_guard<std::mutex> lock(job->mutex);
    stage=job->message;
    return true;
}
void EditorModule::CommitBracketingEdit() {
    if(!m_Bracketing||Stack::Workspace::IsPreview())return;
    auto& ui=*m_Bracketing;
    const auto outcome=Stack::Project::CommitBracketingEdit(ui,
        IsBracketingActive()?m_Project->snapshot.get():nullptr,
        ImGui::IsAnyItemActive(),ImGui::GetTime());
    if(!outcome.changed)return;
    ui.requestDetail=false;
    ui.textureIdentity.clear();
    if(outcome.invalidatedInteractiveRaw)MarkRenderDirty();
    if(outcome.documentChanged)MarkDirty();
}
bool EditorModule::StartBracketingProcessing(bool publish,std::string* error) {
    if(RequestAutoBracketForeground("process this bracket",[this,publish]{StartBracketingProcessing(publish,nullptr);}))return true;
    if(!IsBracketingActive()||Stack::Workspace::IsPreview()) return false;
    TickBracketing();
    if(!m_Bracketing) return false;
    auto& ui=*m_Bracketing;
    if(ui.processRequired) {if(error)*error="The bracket has pending input changes. Press Process or discard them first.";return false;}
    ui.publishRequested|=publish;
    if(ui.job&&!ui.job->done) return true;
    if(ui.result&&ui.completedRecipe==ui.storedRecipe&&ui.completedRevision==m_Project->snapshot->hdrInputRevision&&!ui.pending&&!ui.failed) {
        if(publish) EnterBracketingRaw();return true;
    }
    const auto snapshot=*m_Project->snapshot;
    const auto* set=Stack::Project::FindSourceSet(snapshot,snapshot.activeSourceSetId);
    if(!set||!m_Project->store) return false;
    auto job=std::make_shared<Stack::Editor::BracketingJob>();
    job->projectId=snapshot.projectId;job->setId=set->sourceSetId;job->revision=snapshot.hdrInputRevision;
    job->recipeIdentity=ui.storedRecipe;
    Stack::Project::BracketingJobRequest request;
    request.store=m_Project->store;request.snapshot=snapshot;request.setId=set->sourceSetId;
    request.cacheRoot=AppPaths::GetCacheDirectory();
    request.recipe=ui.recipe;
    if(ui.result&&!ui.failed)request.analysis=ui.result->analysis;
    if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama&&ui.panoramaAnalysis)request.analysis=ui.panoramaAnalysis;
    bool meaningful = !ui.result || ui.failed;
    if (!meaningful && !ui.completedRecipe.empty()) {
        Raw::Bracketing::BracketingRecipe completed;
        std::string recipeError;
        meaningful = !Raw::Bracketing::Deserialize(nlohmann::json::parse(ui.completedRecipe), completed, recipeError, true) ||
            Stack::Project::BracketingInputsChanged(completed, ui.recipe);
    }
    const auto notifier = GetNotifier();
    const auto activity = notifier.BeginActivity("Processing frames", meaningful);
    m_MultiFrameProcessingActivity = activity;
    ui.activityNotifier = notifier; ui.activity = activity; ui.activityMeaningful = meaningful;
    const auto completionLease = Stack::Notifications::RetainAsyncActivity(notifier, activity);
    ui.activityLease = completionLease;
    const auto activityMetadata = Stack::Notifications::ForAsyncActivity(notifier, activity, "Processing frames");
    if(m_RawRenderClientId!=0)request.executeOpenGlTask=[](Raw::OpenGlTask task,std::string& taskError) {
        return Stack::EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(std::move(task),taskError);
    };
    request.didRestoreResult=[job]{job->restoredFromProject=true;};
    request.shouldCancel=[job]{return job->canceled.load();};
    request.reportProgress=[job,notifier,activity](double p,const std::string& message){
        job->progress=p;
        {std::lock_guard<std::mutex> lock(job->mutex);job->message=message;}
        notifier.UpdateActivity(activity, message);
    };
    if(m_RawWorkspaceRootTabActive) {
    static std::atomic<std::uint64_t> nextPresentation{0};
    auto presentation=std::make_shared<Stack::Editor::ProcessingPresentation>();
    presentation->generation=++nextPresentation;presentation->projectId=snapshot.projectId;
    presentation->recipeIdentity=ui.storedRecipe;presentation->began=ImGui::GetTime();presentation->active=true;
    presentation->mailbox=std::make_shared<Stack::Editor::ProcessingMailbox>(presentation->generation,ui.recipe.originFrameId);
    for(std::size_t g=0;g<ui.recipe.groups.size();++g)for(const auto& frame:ui.recipe.groups[g].frames) {
        if((!frame.enabled||!ui.recipe.groups[g].enabled)&&frame.id!=ui.recipe.originFrameId)continue;
        Stack::Editor::ProcessingCard card;card.id=frame.id;card.group=static_cast<unsigned>(g);
        card.label=ui.frameLabels.count(frame.id)?ui.frameLabels[frame.id]:"Capture";
        card.color=Stack::Editor::BracketColor(ui.recipe,g);
        presentation->cards.push_back(std::move(card));
    }
    std::stable_sort(presentation->cards.begin(),presentation->cards.end(),[&](const auto& a,const auto& b){
        return a.id==ui.recipe.originFrameId && b.id!=ui.recipe.originFrameId;
    });
    if(request.analysis && ui.presentation)if(auto previous=ui.presentation->mailbox->Read())
        for(const auto& [id,image]:previous->images) {
            Raw::Bracketing::ProcessingProgress event;event.thumbnail=image;presentation->mailbox->Publish(event);
        }
    job->presentation=presentation->mailbox;ui.presentation=presentation;
    }
    request.reportPresentation=[mailbox=job->presentation,notifier,activity](const Raw::Bracketing::ProcessingProgress& event){
        if(mailbox)mailbox->Publish(event);
        if(event.evidenceOnly || event.thumbnail)return;
        std::optional<Stack::Notifications::Progress> progress;
        if(event.total>0)progress=Stack::Notifications::Progress{double(event.completed),double(event.total),{}};
        notifier.UpdateActivity(activity, Raw::Bracketing::ProcessingStageTitle(event.stage), progress);
    };
    ui.job=job;ui.pending=false;ui.failed=false;ui.status="Preparing bracket...";
    m_MultiFrameGraphProcessingTaskState=Async::TaskState::Running;
    const bool submitted=ProjectTasks().SubmitHighPriority(activityMetadata,[request=std::move(request),job,notifier,activity,completionLease]() mutable {
        job->result=Stack::Project::RunBracketingJob(request);
        Raw::Bracketing::ProcessingProgress terminal;
        terminal.stage=job->canceled?Raw::Bracketing::ProcessingStage::Canceled:
            job->result.status==Raw::Bracketing::BracketingResult::Status::Completed?Raw::Bracketing::ProcessingStage::Completed:Raw::Bracketing::ProcessingStage::Failed;
        if(job->presentation)job->presentation->Publish(terminal);
        job->done.store(true);
    });
    if(!submitted) {if(ui.presentation)ui.presentation->active=false;ui.job.reset();ui.failed=true;ui.status="Could not schedule bracketing.";m_MultiFrameGraphProcessingTaskState=Async::TaskState::Failed;
        notifier.FailActivity(activity,ui.status);ui.activityLease.reset();}
    return submitted;
}
void EditorModule::TickBracketing(bool foreground) {
    if(Stack::Workspace::IsPreview()) return;
    if(m_Bracketing&&!m_Bracketing->awaitingProject.empty()) {
        auto state=m_Bracketing;
        if(IsDeferredLoadedProjectApplyActive())return;
        if(HasDeferredLoadedProjectApplyFailed()) {
            state->awaitingProject.clear();state->failed=true;
            state->status=GetDeferredLoadedProjectStatusText();
            auto completion = std::exchange(state->saveBeforeCloseCompletion, {});
            if (completion) completion(false);
            return;
        }
        if(IsBracketingActive()&&std::filesystem::path(GetCurrentProjectFileName()).lexically_normal()==state->awaitingProject.lexically_normal()) {
            state->awaitingProject.clear();state->newProject=false;
            state->projectId=m_Project->snapshot->projectId;
            state->setId=m_Project->snapshot->activeSourceSetId;
            const bool saved = CommitBracketingDraft(state->processAfterImport,nullptr);
            auto completion = std::exchange(state->saveBeforeCloseCompletion, {});
            if (completion) completion(saved);
            return;
        }
        return;
    }
    if(m_Bracketing&&m_Bracketing->newProject) {
        if(m_Bracketing->boundDocument==GetCurrentProjectFileName()){TickBracketingDraft();return;}
        m_Bracketing.reset();
    }
    if(!IsBracketingActive()) {
        if(m_Bracketing&&m_Bracketing->job) m_Bracketing->job->canceled=true;
        if(m_Bracketing) m_MultiFrameGraphProcessingTaskState=Async::TaskState::Idle;
        m_Bracketing.reset();return;
    }
    const auto& snapshot=*m_Project->snapshot;
    const auto* set=Stack::Project::FindSourceSet(snapshot,snapshot.activeSourceSetId);
    if(!m_Bracketing||m_Bracketing->projectId!=snapshot.projectId||m_Bracketing->setId!=set->sourceSetId) {
        if(m_Bracketing&&m_Bracketing->job) m_Bracketing->job->canceled=true;
        m_Bracketing=std::make_shared<Stack::Editor::BracketingSession>();
        m_Bracketing->projectId=snapshot.projectId;m_Bracketing->setId=set->sourceSetId;
    }
    auto& ui=*m_Bracketing;
    const auto stored=set->settings["bracketing"].dump();
    if(ui.storedRecipe!=stored) {
        if(!Raw::Bracketing::Deserialize(set->settings["bracketing"],ui.recipe,ui.status)) {ui.failed=true;return;}
        ui.storedRecipe=stored;ui.pending=true;ui.changedAt=ImGui::GetTime();
        ui.processRequired=set->settings.contains("bracketingDraft");
        if(ui.processRequired) {
            if(!Raw::Bracketing::Deserialize(set->settings["bracketingDraft"],ui.recipe,ui.status,true)){ui.failed=true;return;}
            ui.pending=false;ui.status="Saved changes pending. Press Process to update the result.";
        }
        ui.editedRecipe=Raw::Bracketing::Serialize(ui.recipe).dump();
        if(!ui.result&&!ui.job&&Stack::Project::HasSavedBracketingResult(snapshot,*set)) {
            auto job=std::make_shared<Stack::Editor::BracketingJob>();
            job->projectId=snapshot.projectId;job->setId=set->sourceSetId;
            job->revision=snapshot.hdrInputRevision;job->recipeIdentity=stored;
            job->restoredFromProject=true;
            ui.job=job;ui.pending=false;ui.publishRequested=true;
            ui.status="Opening saved bracket...";
            m_MultiFrameGraphProcessingTaskState=Async::TaskState::Running;
            const auto notifier = GetNotifier();
            const auto activity = notifier.BeginActivity("Processing frames", false);
            m_MultiFrameProcessingActivity = activity;
            notifier.UpdateActivity(activity,"Opening saved bracket...");
            ui.activityNotifier=notifier;ui.activity=activity;ui.activityMeaningful=false;
            const auto completionLease=Stack::Notifications::RetainAsyncActivity(notifier,activity);
            ui.activityLease=completionLease;
            if(!ProjectTasks().SubmitHighPriority(Stack::Notifications::ForAsyncActivity(notifier,activity,"Processing frames"),
                [job,snapshot,store=m_Project->store,notifier,activity,completionLease] {
                std::string error;
                const bool restored=Stack::Project::RestoreBracketingResult(store,snapshot,job->setId,
                    job->result,error,[job]{return job->canceled.load();});
                if(!restored)job->result.message=error.empty()?
                    "The saved result is unavailable. Press Process to rebuild it from the saved captures.":error;
                job->done=true;
            })) {
                ui.job.reset();ui.failed=true;ui.status="Could not schedule opening the saved bracket.";
                m_MultiFrameGraphProcessingTaskState=Async::TaskState::Failed;
                notifier.FailActivity(activity,ui.status);ui.activityLease.reset();
            }
        }
    }
    TickBracketingDraft();
    if(ui.preparationRequested||ui.preparationJob) {
        Stack::Project::BracketingJobRequest preparation;
        preparation.store=m_Project->store;preparation.snapshot=snapshot;preparation.setId=ui.setId;
        preparation.cacheRoot=AppPaths::GetCacheDirectory();
        if(m_RawRenderClientId!=0)preparation.executeOpenGlTask=[](Raw::OpenGlTask task,std::string& error) {
            return Stack::EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(std::move(task),error);
        };
        auto inspectionActivity=Stack::Notifications::ForAsyncActivity(GetNotifier(),{},"Preparing bracket inspection");
        inspectionActivity.maintenance=true;
        if(Stack::Project::TickBracketingPreparation(ui,std::move(preparation),std::move(inspectionActivity)))ui.textureIdentity.clear();
    }
    if(ui.job&&ui.job->done) {
        auto job=ui.job;ui.job.reset();
        const bool current=Stack::Project::CanAdoptBracketingJob(ui,*job,snapshot,*set);
        if(ui.presentation && ui.presentation->mailbox==job->presentation) {
            ui.presentation->waitingForRaw=current && job->result.status==Raw::Bracketing::BracketingResult::Status::Completed && bool(job->result.raw);
            if(ui.presentation->waitingForRaw) {
                ui.presentation->expectedHash=job->result.raw->contentIdentityHash;
                Raw::Bracketing::ProcessingProgress event;event.stage=Raw::Bracketing::ProcessingStage::RawPreview;
                ui.presentation->mailbox->Publish(event);
            }else ui.presentation->active=false;
        }
        if(current) {
            if(Stack::Project::AdoptBracketingJob(ui,*job)) {
                ui.gradedResult=true;
                if(m_DocumentPersistenceEnabled && !job->restoredFromProject)MarkDirty();
                ui.textureIdentity.clear();
                if(ui.interactiveRaw){ui.interactiveRaw.reset();ClearRawWorkspaceGraphScopeReadbackCaches();MarkRenderRefreshDirty();}
                if(ui.detailMode&&ui.detail.width&&ui.detail.height) {
                    const double scale=ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::SuperResolution2x?2.:1.;
                    ui.probeX=static_cast<float>(std::clamp((ui.detail.sensorOriginX+ui.detail.width*ui.detail.sensorStepX*.5)*scale/
                        ui.result->raw->metadata.visibleWidth,0.,.99999));
                    ui.probeY=static_cast<float>(std::clamp((ui.detail.sensorOriginY+ui.detail.height*ui.detail.sensorStepY*.5)*scale/
                        ui.result->raw->metadata.visibleHeight,0.,.99999));
                    ui.requestDetail=true;
                }
                m_MultiFrameGraphProcessingTaskState=Async::TaskState::Ready;
                ui.activityNotifier.CompleteActivity(ui.activity,
                    ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama ? "Panorama RAW result ready." : "Bracket RAW result ready.",
                    ui.activityMeaningful && !job->restoredFromProject);
            } else {
                m_MultiFrameGraphProcessingTaskState=Async::TaskState::Failed;
                ui.activityNotifier.FailActivity(ui.activity,
                    ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama ? "Panorama processing failed." : "Bracket processing failed.",ui.status);
                if(ui.interactiveRaw){ui.interactiveRaw.reset();ClearRawWorkspaceGraphScopeReadbackCaches();MarkRenderRefreshDirty();}
            }
            m_MultiFrameGraphProcessingStatusText=ui.status;
        } else {
            ui.activityNotifier.CancelActivity(ui.activity,
                job->canceled ? "Bracket processing cancelled." : "Bracket inputs changed. The result was not adopted.");
            m_MultiFrameGraphProcessingTaskState=Async::TaskState::Idle;
            if(ui.presentation)ui.presentation->active=false;
            // A superseded worker is expected to cancel. Only explicit
            // cancellation ends the session; a newer edit must still run.
            if(job->canceled&&!(ui.pending&&ui.publishRequested)) {
                ui.status="Canceled. Previous result retained.";ui.failed=true;ui.previewDirty=false;
                if(ui.interactiveRaw){ui.interactiveRaw.reset();ClearRawWorkspaceGraphScopeReadbackCaches();MarkRenderRefreshDirty();}
            }
        }
        ui.activityLease.reset();
    }
    if(Stack::Project::RefreshBracketingInteractivePreview(ui)) {
        ClearRawWorkspaceGraphScopeReadbackCaches();
        NoteRawWorkspaceRecipePreviewEdit(false);
        MarkRenderRefreshDirty();
    }
    if(ui.detailJob&&ui.detailJob->done) {
        if(!ui.detailJob->canceled&&ui.detailJob->recipeIdentity==stored) {
            if(ui.detailJob->error.empty()&&ui.detailJob->preview.width) {
                ui.detail=std::move(ui.detailJob->preview);ui.detailMode=true;
                ui.zoom=1;ui.panX=ui.panY=0;ui.textureIdentity.clear();
                ui.probeX=ui.probeY=.5f;
            } else ui.status=ui.detailJob->error;
        }
        ui.detailJob.reset();
    }
    if(ui.requestDetail&&ui.result&&(ui.result->analysis||ui.result->panorama)&&!ui.detailJob) {
        ui.requestDetail=false;
        auto job=std::make_shared<Stack::Editor::BracketingSession::DetailJob>();
        job->recipeIdentity=stored;ui.detailJob=job;
        Raw::Bracketing::ProcessingRequest request;request.recipe=ui.recipe;request.analysis=ui.result->analysis;
        const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
        request.memoryBudgetBytes=budget.valid?budget.budgetBytes:0;
        request.shouldCancel=[job]{return job->canceled.load();};
        const unsigned x=static_cast<unsigned>(ui.probeX*ui.result->raw->metadata.visibleWidth);
        const unsigned y=static_cast<unsigned>(ui.probeY*ui.result->raw->metadata.visibleHeight);
        const auto completed=ui.result;
        if(!ProjectTasks().SubmitHighPriority([job,request,x,y,completed] {
            try {job->preview=completed->panorama?Raw::Bracketing::Panorama::NativeDetail(*completed,x,y,512):completed->reconstructionInspection?
                Raw::Bracketing::SuperResolutionDetail(*completed,x,y,512,request.shouldCancel,request.memoryBudgetBytes):Raw::Bracketing::RenderNativeDetail(request,x,y,512);}
            catch(const std::exception& e) {job->error=e.what();}
            job->done=true;
        })) {job->error="Could not schedule native detail.";job->done=true;}
    }
    if(ui.publishRequested&&ui.result&&ui.completedRecipe==stored&&ui.completedRevision==snapshot.hdrInputRevision&&!ui.pending&&!ui.job&&!ui.failed) {
        if(m_HdrAdoptedRawResult&&m_HdrAdoptedRawResult->projectId==snapshot.projectId&&
           m_HdrAdoptedRawResult->sourceSetId==set->sourceSetId&&m_HdrAdoptedRawResult->inputRevision==snapshot.hdrInputRevision&&
           m_HdrAdoptedRawResult->contentHash==ui.result->raw->contentIdentityHash) {
            ui.publishRequested=false;
            if(ui.interactiveRaw) {
                ui.interactiveRaw.reset();
                ClearRawWorkspaceGraphScopeReadbackCaches();
                NoteRawWorkspaceRecipePreviewEdit(false);
                MarkRenderRefreshDirty();
            }
            return;
        }
        HdrAdoptedRawResult adopted;adopted.projectId=snapshot.projectId;adopted.sourceSetId=set->sourceSetId;
        adopted.inputRevision=snapshot.hdrInputRevision;adopted.rawData=ui.result->raw;
        adopted.contentHash=ui.result->raw->contentIdentityHash;
        m_BracketingPresentedSourceHash=0;
        m_HdrAdoptedRawResult=std::move(adopted);ui.publishRequested=false;
        ClearRawWorkspaceGraphScopeReadbackCaches();
        NoteRawWorkspaceRecipePreviewEdit(false);
        Stack::Editor::UpdateBracketingGalleryProject(m_RawWorkspace,snapshot,GetCurrentProjectFileName());
        InvalidateRawWorkspaceGalleryPresentation();
        for(auto& node:m_Project->graph.EditNodes()) if(node.kind==EditorNodeGraph::NodeKind::MultiFrameHdr&&node.multiFrameHdr.sourceSetId==set->sourceSetId) {
            node.multiFrameHdr.resultState="ready";node.multiFrameHdr.presentationStatus="Bracket result ready.";
        }
        m_RawWorkspaceStaleRenderStatusText.clear();m_MultiFrameProjectCoverRefreshPending=true;MarkRenderDirty();
    }
    // A superseded job can finish after navigation has left Bracketing. RAW
    // must start the requested revision without depending on that tab drawing.
    if(Stack::Project::ShouldStartPendingBracketing(ui,ImGui::GetTime(),foreground && ImGui::IsAnyItemActive())) {
        ui.pending=false;
        StartBracketingProcessing(true,nullptr);
    }
}
void EditorModule::EnterBracketingRaw() {
    if(!IsBracketingActive()||Stack::Workspace::IsPreview()) return;
    TickBracketing();if(!m_Bracketing) return;
    if(m_Bracketing->failed||m_Bracketing->processRequired) return;
    if(!m_Bracketing->pending&&!m_Bracketing->job&&m_HdrAdoptedRawResult&&
       m_HdrAdoptedRawResult->projectId==m_Bracketing->projectId&&
       m_HdrAdoptedRawResult->sourceSetId==m_Bracketing->setId&&
       m_HdrAdoptedRawResult->inputRevision==m_Project->snapshot->hdrInputRevision) return;
    m_Bracketing->publishRequested=true;
    if(m_Bracketing->pending||(!m_Bracketing->result&&!m_Bracketing->job)) StartBracketingProcessing(true,nullptr);
    else TickBracketing();
}
