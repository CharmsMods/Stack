#pragma once
#include "AutoBracketQueue.h"
#include "Project/BracketingJobRunner.h"
#include "Project/BracketingProgress.h"
#include "Notifications/AsyncActivity.h"
#include <atomic>
#include <mutex>

namespace Stack::AutoBracket {
struct Work {
    std::atomic<bool> cancel{false},done{false};
    std::atomic<double> progress{0};
    std::mutex mutex;
    std::string status="Preparing bracket...";
    Item item;
    std::filesystem::path root,cacheRoot;
    std::uint64_t generation=0;
    Project::ProjectStoreOpenResult project;
    std::shared_ptr<const Raw::Bracketing::BracketingResult> result;
    std::shared_ptr<Project::ProcessingMailbox> mailbox;
    State outcome=State::Failed;
    bool storageFailure=false;
    bool imageProduced=false;
    Notifications::Notifier notifier;
    Notifications::ActivityHandle activity;
    std::shared_ptr<Notifications::AsyncActivityCompletion> activityLease;
    std::vector<unsigned char> coverPixels;
    unsigned coverWidth=0,coverHeight=0;
};
class AutoBracketCoordinator {
public:
    void Initialize();
    void SetNotificationScope(Notifications::Notifier notifier) { notifier_=std::move(notifier); }
    void Shutdown();
    bool SetRoot(const std::filesystem::path&,double now);
    void UpdateCandidates(const std::vector<Candidate>&,
        const std::map<std::string,std::pair<std::string,std::filesystem::path>>& represented);
    void Tick(double now,bool ready,bool foregroundBusy,const Raw::OpenGlTaskExecutor&,const std::string& foregroundProjectId = {});
    bool CanStartWork(double now,bool ready,bool foregroundBusy,const std::string& foregroundProjectId = {}) const;
    void SetProtectedProjectIds(const std::vector<std::string>& projectIds,
        const std::vector<std::filesystem::path>& paths = {});
    bool IsWritingProject(const std::filesystem::path&) const;
    void NoteActivity(double now) {lastActivity_=now;}
    void RunNow();
    void SetEnabled(bool);
    void SetPaused(bool);
    void PauseUntilIdle(double now);
    void Retry(const std::string&);
    void Exclude(const std::string&);
    bool RequestForeground(std::string label,std::function<void()> action);
    void ResolveForeground(bool cancelCurrent);
    void CancelCurrent();
    void DismissForeground();
    bool Busy() const {return work_&&!work_->done;}
    bool HasWork() const {return work_!=nullptr;}
    bool Enabled() const {return enabled_;}
    bool WaitingForChoice() const {return pendingAction_&&!takeoverChosen_;}
    bool Yielding() const {return pendingAction_!=nullptr;}
    const std::string& ActionLabel() const {return actionLabel_;}
    std::uint64_t ForegroundRequestId() const { return foregroundRequestId_; }
    const Queue& GetQueue() const {return queue_;}
    const std::string& Error() const {return error_;}
    std::shared_ptr<Work> Current() const {return work_;}
    std::shared_ptr<Work> ConsumeCompleted();
    double IdleSeconds(double now) const {return std::max(0.0,now-lastActivity_);}
    std::uint64_t Revision() const {return revision_;}
private:
    static constexpr double kResumeIdleSeconds = 60.0;
    bool Persist();
    void Begin(Item&,const Raw::OpenGlTaskExecutor&);
    bool IsPendingWorkEligible(const Item&,const std::string& foregroundProjectId) const;
    Queue queue_;
    bool enabled_=true,manualRun_=false,takeoverChosen_=false,loaded_=false,storageBlocked_=false;
    double lastActivity_=0;
    std::uint64_t generation_=0,revision_=0;
    std::uint64_t foregroundRequestId_=0;
    std::string error_,actionLabel_;
    std::shared_ptr<Work> work_,completed_;
    std::function<void()> pendingAction_;
    Notifications::Notifier notifier_;
    std::vector<std::string> protectedProjectIds_;
    std::vector<std::filesystem::path> protectedProjectPaths_;
    bool IsProtectedPath(const std::filesystem::path&) const;
};
void ExecuteWork(const std::shared_ptr<Work>&,const Raw::OpenGlTaskExecutor&);
}
