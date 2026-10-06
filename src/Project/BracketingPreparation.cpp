#include "BracketingPreparation.h"
#include "Async/TaskSystem.h"

namespace Stack::Project {
bool TickBracketingPreparation(BracketingState& ui,BracketingJobRequest request,Async::ActivityMetadata activity) {
    bool changed=false;
    if(ui.preparationJob&&ui.preparationJob->done) {
        auto job=std::move(ui.preparationJob);ui.preparationJob.reset();
        if(ui.result&&job->projectId==ui.projectId&&job->setId==ui.setId&&
            job->revision==ui.completedRevision&&job->recipeIdentity==ui.completedRecipe) {
            if(!job->canceled&&job->result.status==Raw::Bracketing::BracketingResult::Status::Completed) {
                auto ready=std::make_shared<Raw::Bracketing::BracketingResult>(*ui.result);
                ready->analysis=job->result.analysis;ready->preview.originals=ready->analysis->originals;
                ui.result=std::move(ready);ui.preview.originals=ui.result->analysis->originals;
                changed=true;ui.inspectionError.clear();ui.preparationFailed=false;
            }else {
                ui.inspectionError=job->canceled?"Inspection preparation canceled.":job->result.message;
                ui.preparationFailed=true;
            }
        }
    }
    if(!ui.preparationRequested||ui.preparationJob||ui.job||!ui.result)return changed;
    ui.preparationRequested=false;
    if(ui.result->analysis)return changed;
    std::string error;
    if(!Raw::Bracketing::Deserialize(Project::json::parse(ui.completedRecipe),request.recipe,error)) {
        ui.inspectionError=error;ui.preparationFailed=true;return changed;
    }
    auto job=std::make_shared<BracketingJob>();job->projectId=ui.projectId;job->setId=ui.setId;
    job->revision=ui.completedRevision;job->recipeIdentity=ui.completedRecipe;
    request.preparationOnly=true;request.restoreSavedResult=false;
    request.shouldCancel=[job]{return job->canceled.load();};
    request.reportProgress=[job](double value,const std::string& status) {
        job->progress=value;std::lock_guard<std::mutex> lock(job->mutex);job->message=status;
    };
    ui.preparationJob=job;
    activity.label="Preparing bracket inspection";
    activity.maintenance=true;
    if(!Async::TaskSystem::Get().SubmitHighPriority(std::move(activity),[request=std::move(request),job] {
        job->result=RunBracketingJob(request);job->done=true;
    })) {
        job->result.message="Could not schedule bracket inspection.";job->done=true;
    }
    return changed;
}
}
