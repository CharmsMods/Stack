#include "Storage/ScratchBudget.h"
#include "BracketingJobRunner.h"
#include "BurstEngine/Client.h"
#include "BurstEngine/ScratchDirectory.h"
#include "Persistence/BracketingResultStore.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Persistence/BracketMaster.h"
#include "Raw/ReconstructionDepth.h"
#include "Persistence/ManagedAssetPaths.h"
#include <algorithm>
#include <future>
#include <mutex>
#include <unordered_map>

namespace Stack::Project {
Raw::Bracketing::BracketingResult OpenOrRegenerateBracket(const BracketingJobRequest& job) {
    using Result = Raw::Bracketing::BracketingResult;
    Result result;
    if (RestoreBracketingResult(job.store, job.snapshot, job.setId, result, result.message, job.shouldCancel)) {
        if (job.didRestoreResult) job.didRestoreResult();
        return result;
    }
    if (!job.store || job.store->StorageKind() != ProjectStorageKind::WorkingFile) return result;
    const auto* set = FindSourceSet(job.snapshot, job.setId);
    if (!set || !HasSavedBracketingResult(job.snapshot, *set)) return result;
    const auto& saved = set->settings.at("bracketingResult");
    std::string key = ClosetAssetRoot(job.store->StoragePath()).generic_u8string() + "|" +
        saved.at("identity").get<std::string>() + "|" + saved.at("recipe").get<std::string>();

    struct Flight {
        std::vector<std::function<bool()>> canceled;
        std::promise<Result> completion;
        std::shared_future<Result> result = completion.get_future().share();
    };
    static std::mutex mutex;
    static std::unordered_map<std::string, std::shared_ptr<Flight>> flights;
    std::shared_ptr<Flight> flight;
    bool producer = false;
    {
        std::lock_guard lock(mutex);
        auto& slot = flights[key];
        if (!slot) { slot = std::make_shared<Flight>(); producer = true; }
        flight = slot;
        flight->canceled.push_back(job.shouldCancel);
    }
    if (!producer) {
        while (flight->result.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            if (job.shouldCancel && job.shouldCancel()) { result.status = Result::Status::Canceled; return result; }
        }
        return flight->result.get();
    }
    try {
        auto recovery = job;
        recovery.restoreSavedResult = false;
        recovery.backgroundProcessing = true;
        recovery.shouldCancel = [flight] {
            std::lock_guard lock(mutex);
            return std::all_of(flight->canceled.begin(), flight->canceled.end(),
                [](const auto& canceled) { return canceled && canceled(); });
        };
        result = RunBracketingJob(recovery);
        // RunBracketingJob publishes the complete measurement before waking other editors.
    } catch (const std::exception& error) {
        result.status = Result::Status::Failed;
        result.message = error.what();
    }
    flight->completion.set_value(std::move(result));
    {
        std::lock_guard lock(mutex);
        flights.erase(key);
    }
    return flight->result.get();
}

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
        // Background work limits concurrency, never the alignment quality plan.
        request.memoryBudgetBytes=budget.budgetBytes;
        request.automaticMemoryBudget=true;
        request.terminateOnCancel=job.backgroundProcessing;
        request.workerCount=ResolveBracketingWorkerCount(request.memoryBudgetBytes,job.workerLimit);
        request.preferGpuRegistration=true;request.executeOpenGlTask=job.executeOpenGlTask;
        request.shouldCancel=job.shouldCancel;request.reportProgress=job.reportProgress;
        request.reportPresentation=job.reportPresentation;
        request.removeCacheOnRelease=true;
        auto sourceScratch=std::make_shared<BurstEngine::ScratchDirectory>(job.cacheRoot,BurstEngine::ScratchKind::Sources);
        request.cacheDirectory=sourceScratch->Path()/"prepared";
        sourcesDirectory=request.cacheDirectory.parent_path()/"sources";
        for(const auto& frame:set->frames) {
            if(!std::any_of(job.recipe.groups.begin(),job.recipe.groups.end(),[&](const auto& group){
                return std::any_of(group.frames.begin(),group.frames.end(),[&](const auto& f){return f.id==frame.frameId &&
                    (f.id==job.recipe.originFrameId || (group.enabled && f.enabled));});
            }))continue;
            const auto* asset=Project::FindEmbeddedAsset(job.snapshot,frame.assetId);
            if(!asset)throw std::runtime_error("A bracket source asset is missing.");
            const auto ownedPath = job.store->ResolveAssetFile(frame.assetId);
            if (ownedPath.empty() && job.store->StorageKind() == Project::ProjectStorageKind::WorkingFile)
                throw std::runtime_error("Locate the original capture before reprocessing: " + asset->originalFilename);
            request.sources.push_back({frame.frameId,asset->sha256,asset->byteLength,
                ownedPath.empty() ? sourcesDirectory/(asset->sha256+std::filesystem::path(asset->originalFilename).extension().string()) : ownedPath,
                asset->originalFilename});
        }
        const bool reuse=Stack::BurstEngine::HasLiveSession(request.analysis)&&
            request.analysis->identity==AnalysisIdentity(request);
        if(!reuse) {
            std::filesystem::create_directories(sourcesDirectory);
            std::size_t copied=0;
            for(const auto& source:request.sources) {
                if(canceled())break;
                ReportPresentation(request,ProcessingStage::Assets,copied++,request.sources.size(),source.frameId);
                const auto frame=std::find_if(set->frames.begin(),set->frames.end(),[&](const auto& f){return f.frameId==source.frameId;});
                std::string error;
                if(job.store->StorageKind()==Project::ProjectStorageKind::PortableFile) {
                    if(!Stack::Storage::ScratchBudget::ReserveFile(source.path, source.bytes))
                        throw std::runtime_error(Stack::Storage::ScratchBudget::Failure);
                    if(!job.store->CopyAssetToFile(frame->assetId,source.path,&error))throw std::runtime_error(error);
                }
            }
        }
        result=Stack::BurstEngine::Process(request);
        if (!canceled() && !job.preparationOnly && result.status==BracketingResult::Status::Completed &&
            job.store->StorageKind()==ProjectStorageKind::WorkingFile &&
            set->settings.contains("bracketing") && set->settings.at("bracketing")==Serialize(job.recipe)) {
            try {
                PublishBracketMaster(job.store,job.snapshot,*set,result);
            } catch(const std::exception& e) {
                const auto depth=result.raw->reconstructionDepth;
                if(!depth || depth->masks.empty() || canceled())throw;
                result.raw->reconstructionDepth=Raw::UnavailableDepth(depth->sensorWidth,depth->sensorHeight,e.what());
                PublishBracketMaster(job.store,job.snapshot,*set,result);
                result.message+=" "+result.raw->reconstructionDepth->message;
            }
        }
        if(job.backgroundProcessing&&result.status==BracketingResult::Status::Failed&&
            result.message.find("memory budget")!=std::string::npos)
            result.message+=" Close other memory-heavy work and retry; automatic bursts use the same quality requirements as foreground processing.";
    }catch(const std::exception& error){result.status=BracketingResult::Status::Failed;result.message=error.what();}
    if(job.shouldCancel&&job.shouldCancel())result.status=BracketingResult::Status::Canceled;
    return result;
}
}
