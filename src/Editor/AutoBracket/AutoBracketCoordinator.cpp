#include "AutoBracketCoordinator.h"
#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Persistence/ProjectIndex.h"
#include "Project/ProjectPath.h"
#include "Raw/Internal/RawWorkspaceStorageIO.h"
#include <algorithm>
#include <thread>

namespace Stack::AutoBracket {
namespace {
std::filesystem::path PreferencePath(){return AppPaths::GetSettingsDirectory()/"auto-bracket.json";}
struct Dispatch {
    std::shared_ptr<Work> work;
    std::atomic<bool> started{false};
    ~Dispatch() {
        // The shared task pool may discard queued work during shutdown.
        if(!started&&!work->done) {
            work->outcome=State::Pending;work->cancel=true;work->done=true;
            work->notifier.CancelActivity(work->activity,"Automatic bracketing cancelled before execution.");
        }
    }
};
}
void AutoBracketCoordinator::Initialize() {
    Project::json value;if(RawWorkspace::StorageIO::ReadJsonFile(PreferencePath(),value)&&value.is_object()&&
        value.contains("enabled")&&value["enabled"].is_boolean())
        enabled_=value.value("enabled",true);
}
void AutoBracketCoordinator::Shutdown() {
    pendingAction_={};manualRun_=false;
    if(work_) {
        work_->cancel=true;
        while(!work_->done)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        for(auto& item:queue_.items)if(item.candidate.identity==work_->item.candidate.identity) {
            item.state=work_->outcome;
            item.error=work_->status;
        }
        if(work_->storageFailure){queue_.paused=true;queue_.resumeWhenIdle=false;}
        Persist();work_.reset();
    }
    completed_.reset();
}
bool AutoBracketCoordinator::SetRoot(const std::filesystem::path& root,double now) {
    if(queue_.root==root&&loaded_)return true;
    if(work_){work_->cancel=true;return false;}
    queue_={};queue_.root=root;lastActivity_=now;manualRun_=false;completed_.reset();error_.clear();
    loaded_=true;storageBlocked_=false;
    if(!root.empty()&&!LoadQueue(root,queue_,error_))storageBlocked_=true;
    ++revision_;return true;
}
bool AutoBracketCoordinator::Persist() {
    ++revision_;
    if(storageBlocked_)return false;
    if(queue_.root.empty())return true;
    if(!SaveQueue(queue_,error_)){queue_.paused=true;queue_.resumeWhenIdle=false;storageBlocked_=true;return false;}
    error_.clear();
    return true;
}
void AutoBracketCoordinator::UpdateCandidates(const std::vector<Candidate>& candidates,
    const std::map<std::string,std::pair<std::string,std::filesystem::path>>& represented) {
    Reconcile(queue_,candidates,represented);Persist();
}
void AutoBracketCoordinator::SetEnabled(bool enabled) {
    if(!RawWorkspace::StorageIO::WriteJsonFile(PreferencePath(),{{"enabled",enabled}},&error_))return;
    enabled_=enabled;if(!enabled)manualRun_=false;++revision_;
}
void AutoBracketCoordinator::RunNow(){manualRun_=true;SetPaused(false);}
void AutoBracketCoordinator::SetPaused(bool paused) {
    queue_.paused=paused;queue_.resumeWhenIdle=false;if(paused)manualRun_=false;
    // An explicit resume can retry a failed catalog write; corrupt queues remain protected.
    if(storageBlocked_&&!paused) {
        Queue read;std::string error;
        if(!LoadQueue(queue_.root,read,error)){queue_.paused=true;error_=error;return;}
        storageBlocked_=false;
    }
    Persist();
}
void AutoBracketCoordinator::PauseUntilIdle(double now) {
    queue_.paused=true;
    queue_.resumeWhenIdle=!storageBlocked_;
    manualRun_=false;
    lastActivity_=now;
    Persist();
}
void AutoBracketCoordinator::Retry(const std::string& identity) {
    for(auto& item:queue_.items)if(item.candidate.identity==identity&&item.state!=State::Running) {
        if(item.state==State::Excluded){item.projectId.clear();item.projectPath.clear();}
        item.state=State::Pending;item.error.clear();break;
    }
    Persist();
}
void AutoBracketCoordinator::Exclude(const std::string& identity) {
    for(auto& item:queue_.items)if(item.candidate.identity==identity&&item.state!=State::Running)item.state=State::Excluded;
    Persist();
}
bool AutoBracketCoordinator::RequestForeground(std::string label,std::function<void()> action) {
    if(pendingAction_)return true;
    if(!work_)return false;
    pendingAction_=std::move(action);actionLabel_=std::move(label);takeoverChosen_=false;manualRun_=false;
    ++foregroundRequestId_;
    ++revision_;return true;
}
void AutoBracketCoordinator::ResolveForeground(bool cancelCurrent) {
    takeoverChosen_=true;manualRun_=false;if(cancelCurrent)CancelCurrent();
}
void AutoBracketCoordinator::DismissForeground(){pendingAction_={};actionLabel_.clear();takeoverChosen_=false;}
void AutoBracketCoordinator::CancelCurrent(){if(work_)work_->cancel=true;manualRun_=false;}
void AutoBracketCoordinator::SetProtectedProjectIds(const std::vector<std::string>& projectIds,
    const std::vector<std::filesystem::path>& paths) {
    protectedProjectIds_=projectIds;
    protectedProjectPaths_=paths;
    if(work_&&(IsProtectedPath(work_->item.projectPath)||
        std::find(protectedProjectIds_.begin(),protectedProjectIds_.end(),work_->item.projectId)!=protectedProjectIds_.end()))
        CancelCurrent();
}
bool AutoBracketCoordinator::IsProtectedPath(const std::filesystem::path& path) const {
    return std::any_of(protectedProjectPaths_.begin(),protectedProjectPaths_.end(),
        [&](const auto& protectedPath){return Project::SameProjectPath(path,Project::ResolveProjectStoreRoot(protectedPath));});
}
bool AutoBracketCoordinator::IsWritingProject(const std::filesystem::path& path) const {
    return work_&&!work_->done&&Project::SameProjectPath(work_->item.projectPath,Project::ResolveProjectStoreRoot(path));
}
bool AutoBracketCoordinator::IsPendingWorkEligible(const Item& item,const std::string& foregroundProjectId) const {
    return item.available&&item.state==State::Pending&&!IsProtectedPath(item.projectPath)&&
        (item.projectId.empty()||(item.projectId!=foregroundProjectId&&
            std::find(protectedProjectIds_.begin(),protectedProjectIds_.end(),item.projectId)==protectedProjectIds_.end()));
}
std::shared_ptr<Work> AutoBracketCoordinator::ConsumeCompleted(){auto result=std::move(completed_);completed_.reset();return result;}
void AutoBracketCoordinator::Begin(Item& item,const Raw::OpenGlTaskExecutor& executor) {
    // The idle wait applies only after a user stop. Once resumed, continue
    // through the queue even while the user edits another project.
    queue_.paused=false;queue_.resumeWhenIdle=false;
    if(item.projectId.empty()) {
        item.projectId=Project::GenerateStableUuid();
        item.projectPath=Project::ProjectIndex::BuildUniqueProjectPath(
            RawWorkspace::BuildManagedLayout(queue_.root).projectsDirectory,item.candidate.name,item.projectId);
    }
    item.state=State::Running;item.error.clear();
    if(!Persist()){item.state=State::Pending;notifier_.Error("Automatic bracketing could not save its queue.","Bracket queue",error_);return;}
    auto work=std::make_shared<Work>();work->item=item;work->root=queue_.root;work->generation=++generation_;
    work->cacheRoot=AppPaths::GetCacheDirectory();
    work->mailbox=std::make_shared<Project::ProcessingMailbox>(work->generation);
    work->notifier=notifier_;
    Notifications::NoticeSpec notice;
    notice.title="Automatic bracketing";notice.context=item.candidate.name;
    notice.message="Preparing bracket...";notice.maintenance=!manualRun_;
    work->activity=work->notifier.BeginActivity(std::move(notice));
    work->activityLease=Notifications::RetainAsyncActivity(work->notifier,work->activity);
    auto metadata=Notifications::ForAsyncActivity(work->notifier,work->activity,"Automatic bracketing");
    metadata.maintenance=!manualRun_;
    work_=work;
    auto dispatch=std::make_shared<Dispatch>();dispatch->work=work;
    if(!Async::TaskSystem::Get().Submit(std::move(metadata),[dispatch,executor]{
        dispatch->started=true;ExecuteWork(dispatch->work,executor);
    })) {
        work->status="Could not schedule automatic bracketing.";work->outcome=State::Failed;work->done=true;
        work->notifier.FailActivity(work->activity,work->status);
    }
}
void AutoBracketCoordinator::Tick(double now,bool ready,bool foregroundBusy,const Raw::OpenGlTaskExecutor& executor,const std::string& foregroundProjectId) {
    if(work_&&work_->done) {
        for(auto& item:queue_.items)if(item.candidate.identity==work_->item.candidate.identity) {
            item.state=work_->outcome;item.error=work_->status;break;
        }
        if(work_->storageFailure){queue_.paused=true;queue_.resumeWhenIdle=false;manualRun_=false;}
        if(work_->cancel)lastActivity_=now;
        completed_=work_;work_.reset();Persist();
    }
    if(pendingAction_) {
        if(takeoverChosen_&&!work_) {
            auto action=std::move(pendingAction_);pendingAction_={};takeoverChosen_=false;
            lastActivity_=now;actionLabel_.clear();action();
        }
        return;
    }
    if(!CanStartWork(now,ready,foregroundBusy,foregroundProjectId)) {
        if(manualRun_&&!work_&&!completed_&&std::none_of(queue_.items.begin(),queue_.items.end(),
            [&](const auto& item){return IsPendingWorkEligible(item,foregroundProjectId);}))manualRun_=false;
        return;
    }
    auto item=std::find_if(queue_.items.begin(),queue_.items.end(),[&](const auto& item){return IsPendingWorkEligible(item,foregroundProjectId);});
    Begin(*item,executor);
}
bool AutoBracketCoordinator::CanStartWork(double now,bool ready,bool foregroundBusy,const std::string& foregroundProjectId) const {
    if(work_||completed_||pendingAction_||!loaded_||queue_.root.empty()||storageBlocked_||!ready||foregroundBusy)return false;
    if(queue_.paused&&(!queue_.resumeWhenIdle||IdleSeconds(now)<kResumeIdleSeconds))return false;
    if(!manualRun_&&!enabled_)return false;
    return std::any_of(queue_.items.begin(),queue_.items.end(),[&](const auto& item){
        return IsPendingWorkEligible(item,foregroundProjectId);
    });
}
}
