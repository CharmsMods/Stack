#include "BracketingJobRunner.h"
#include "Persistence/BracketingResultStore.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include <algorithm>

namespace Stack::Project {
Raw::Bracketing::BracketingResult RunBracketingJob(const BracketingJobRequest& job) {
    using namespace Raw::Bracketing;
    BracketingResult result;
    std::filesystem::path sourcesDirectory;
    try {
        const auto canceled=[&]{return job.shouldCancel&&job.shouldCancel();};
        if(canceled()){result.status=BracketingResult::Status::Canceled;return result;}
        const auto* set=Project::FindSourceSet(job.snapshot,job.setId);
        if(!job.store||!set)throw std::runtime_error("The bracket project is unavailable.");
        if(job.restoreSavedResult && !job.preparationOnly && Project::RestoreBracketingResult(job.store,job.snapshot,job.setId,result,
            result.message,job.shouldCancel)){if(job.didRestoreResult)job.didRestoreResult();return result;}
        if(canceled()){result.status=BracketingResult::Status::Canceled;return result;}
        if(job.cacheRoot.empty())throw std::runtime_error("The bracket cache root is unavailable.");
        ProcessingRequest request;
        request.recipe=job.recipe;request.analysis=job.analysis;request.preparationOnly=job.preparationOnly;
        const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
        if(!budget.valid)throw std::runtime_error(budget.message);
        request.memoryBudgetBytes=budget.budgetBytes;request.automaticMemoryBudget=true;
        request.workerCount=ResolveBracketingWorkerCount(request.memoryBudgetBytes);
        request.preferGpuRegistration=true;request.executeOpenGlTask=job.executeOpenGlTask;
        request.shouldCancel=job.shouldCancel;request.reportProgress=job.reportProgress;
        request.reportPresentation=job.reportPresentation;
        request.removeCacheOnRelease=true;
        request.cacheDirectory=job.cacheRoot/"Bracketing"/job.snapshot.projectId.substr(0,12)/
            Project::GenerateStableUuid().substr(0,12)/"prepared";
        sourcesDirectory=request.cacheDirectory.parent_path()/"sources";
        for(const auto& frame:set->frames) {
            if(!std::any_of(job.recipe.groups.begin(),job.recipe.groups.end(),[&](const auto& group){
                return std::any_of(group.frames.begin(),group.frames.end(),[&](const auto& f){return f.id==frame.frameId;});
            }))continue;
            const auto* asset=Project::FindEmbeddedAsset(job.snapshot,frame.assetId);
            if(!asset)throw std::runtime_error("A bracket source asset is missing.");
            request.sources.push_back({frame.frameId,asset->sha256,asset->byteLength,
                sourcesDirectory/(asset->sha256+std::filesystem::path(asset->originalFilename).extension().string()),
                asset->originalFilename});
        }
        const bool reuse=request.analysis&&request.analysis->identity==AnalysisIdentity(request);
        if(!reuse) {
            std::filesystem::create_directories(sourcesDirectory);
            std::size_t copied=0;
            for(const auto& source:request.sources) {
                if(canceled())break;
                ReportPresentation(request,ProcessingStage::Assets,copied++,request.sources.size(),source.frameId);
                const auto frame=std::find_if(set->frames.begin(),set->frames.end(),[&](const auto& f){return f.frameId==source.frameId;});
                std::string error;
                if(!job.store->CopyAssetToFile(frame->assetId,source.path,&error))throw std::runtime_error(error);
            }
        }
        result=Process(request);
    }catch(const std::exception& error){result.status=BracketingResult::Status::Failed;result.message=error.what();}
    if(job.shouldCancel&&job.shouldCancel())result.status=BracketingResult::Status::Canceled;
    if(!sourcesDirectory.empty()){std::error_code ec;std::filesystem::remove_all(sourcesDirectory,ec);}
    return result;
}
}
