#include "UiProcessingLoad.h"
#include "BurstEngine/Client.h"
#include "Persistence/BracketingProject.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include <chrono>
#include <fstream>
#include <set>
#include <algorithm>

UiProcessingLoad::UiProcessingLoad(std::vector<std::filesystem::path> sources,std::filesystem::path output) {
    worker=std::thread([this,sources=std::move(sources),output=std::move(output)]() mutable {
        std::ofstream log(output/"processing.csv");
        log<<"job,seconds,status,message\n";
        try {
            Raw::Bracketing::ProcessingRequest request;
            Stack::Project::RawProjectSnapshot snapshot;
            Stack::Project::MultiFrameSourceSet set;
            std::set<std::string> seen;
            busy=true;
            // Pick a reproducible compatible run from the real input folder,
            // independent of catalog batch arrival order. Preparation stays on
            // this worker, and originals are never copied or modified.
            if(!sources.empty()) {
                const auto root=sources.front().parent_path();sources.clear();
                for(const auto& entry:std::filesystem::directory_iterator(root))
                    if(entry.is_regular_file() && Raw::RawLoader::IsRawPath(entry.path().u8string()))sources.push_back(entry.path());
                std::sort(sources.begin(),sources.end());
            }
            Raw::RawMetadata reference;
            std::ofstream inputs(output/"processing-inputs.txt");
            for(const auto& path:sources) {
                if(stop)break;
                Raw::RawMetadata metadata;
                if(!Raw::RawLoader::LoadMetadata(path.u8string(),metadata))continue;
                if(request.sources.empty()) reference=metadata;
                else if(metadata.orientation!=reference.orientation || metadata.cameraModel!=reference.cameraModel)continue;
                const auto identity=Stack::RawEvidence::ComputeSourceIdentity(path);
                if(!identity.valid)throw std::runtime_error(identity.reason);
                if(!seen.insert(identity.sha256).second)continue;
                Stack::Project::EmbeddedAssetRecord asset;asset.assetId=identity.sha256;
                asset.captureMetadataSummary=Stack::Project::SerializeRawCaptureCompatibilitySummary(
                    Stack::Project::BuildRawCaptureCompatibilitySummary(metadata));
                snapshot.embeddedAssets.push_back(asset);
                Stack::Project::SourceSetFrame frame;frame.frameId=identity.sha256;frame.assetId=identity.sha256;
                set.frames.push_back(frame);
                request.sources.push_back({frame.frameId,identity.sha256,identity.byteSize,path,path.filename().u8string()});
                inputs<<path.u8string()<<','<<identity.sha256<<'\n';
                if(request.sources.size()==3)break;
            }
            if(!stop && request.sources.size()<2)throw std::runtime_error("Processing workload requires at least two distinct real RAWs");
            request.recipe=Stack::Project::SuggestBracketingGroups(snapshot,set);
            request.recipe.reconstruction=Raw::Bracketing::ReconstructionMode::Standard;
            const auto memory=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
            if(!memory.valid)throw std::runtime_error(memory.message);
            request.memoryBudgetBytes=memory.budgetBytes;request.automaticMemoryBudget=true;
            request.workerCount=Raw::Bracketing::ResolveBracketingWorkerCount(memory.budgetBytes);
            request.removeCacheOnRelease=true;request.terminateOnCancel=true;
            request.shouldCancel=[this]{return stop.load();};
            while(!stop && requests<16) {
                const unsigned job=++requests;
                request.cacheDirectory=output/("processing-"+std::to_string(job));
                const auto start=std::chrono::steady_clock::now();
                auto result=Stack::BurstEngine::Process(request);
                const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                log<<job<<','<<seconds<<','<<static_cast<int>(result.status)<<",\""<<result.message<<"\"\n";
                if(result.status==Raw::Bracketing::BracketingResult::Status::Completed)++completed;
                else if(!stop) {failed=true;break;}
            }
        } catch(const std::exception& e) {failed=true;log<<"0,0,error,\""<<e.what()<<"\"\n";}
        busy=false;done=true;
    });
}
UiProcessingLoad::~UiProcessingLoad(){Cancel();if(worker.joinable())worker.join();}
