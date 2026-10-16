#include "Batch.h"
#include "GpuContext.h"
#include "Protocol.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace Stack::BurstEngine {
namespace {
struct ParentLess {
    bool operator()(const std::filesystem::path& left,const std::filesystem::path& right)const {
        const auto a=left.native(),b=right.native();
        return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_LESS_THAN;
    }
};
struct TemporaryWork {
    std::filesystem::path path;
    TemporaryWork() {
        path=std::filesystem::temp_directory_path()/L"StackBurst"/L"Work"/TextPath(UniqueId());
        if(!std::filesystem::create_directories(path))throw std::runtime_error("Could not reserve temporary burst storage.");
    }
    ~TemporaryWork(){std::error_code error;std::filesystem::remove_all(path,error);}
};
std::filesystem::path NewFolder(const std::filesystem::path& parent) {
    for(unsigned i=0;i<10000;++i){
        auto name=L"Stack Burst"+(i?L" ("+std::to_wstring(i+1)+L")":std::wstring());
        const auto path=parent/name;std::error_code error;
        if(std::filesystem::create_directory(path,error))return path;
        if(error&&error!=std::errc::file_exists)throw std::runtime_error("Could not create the output folder: "+error.message());
    }
    throw std::runtime_error("Could not reserve a new output folder.");
}
}
BatchResult RunBatch(const SelectionResult& plan,const BatchOptions& options,
    const std::function<bool()>& shouldCancel,const std::function<void(const BatchProgress&)>& reportProgress){
    BatchResult batch;
    const auto canceled=[&]{return shouldCancel&&shouldCancel();};
    if(plan.canceled||!plan.error.empty()||plan.bursts.empty()||options.formats.empty()){
        batch.canceled=plan.canceled;batch.failures.push_back({"Selection",plan.error.empty()?"Choose at least one output format and a compatible burst set.":plan.error});return batch;
    }
    try{
        TemporaryWork work;GpuContext gpu;
        std::map<std::filesystem::path,std::filesystem::path,ParentLess> folders;
        for(std::size_t index=0;index<plan.bursts.size();++index){
            if(canceled()){batch.canceled=true;break;}
            const auto& set=plan.bursts[index];
            const auto publish=[&](double progress,const std::string& detail){if(reportProgress)reportProgress({index+1,plan.bursts.size(),batch.outputs.size(),
                (double(index)+std::clamp(progress,0.0,1.0))/double(plan.bursts.size()),detail});};
            try{
                const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
                if(!budget.valid)throw std::runtime_error(budget.message);
                Raw::Bracketing::ProcessingRequest request;request.sources=set.sources;request.recipe=set.recipe;
                request.memoryBudgetBytes=budget.budgetBytes;request.automaticMemoryBudget=true;
                request.workerCount=Raw::Bracketing::ResolveBracketingWorkerCount(budget.budgetBytes);
                request.preferGpuCombining=options.preferGpuCombining;
                request.cacheDirectory=work.path/std::to_wstring(index);request.removeCacheOnRelease=true;
                request.shouldCancel=canceled;request.executeOpenGlTask=gpu.Executor();
                // Scalar progress does not enable the costly inspection evidence stream.
                request.reportProgress=[&](double fraction,const std::string& detail){publish(.85*fraction,detail);};
                const auto processStart=std::chrono::steady_clock::now();
                auto result=Raw::Bracketing::Process(request);
                BatchMeasurement measurement;measurement.set=set.name;measurement.recipe=request.recipe;
                measurement.sources=request.sources;measurement.memoryBudgetBytes=request.memoryBudgetBytes;
                measurement.requestedWorkers=request.workerCount;measurement.resultIdentity=result.identity;
                measurement.processingSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-processStart).count();
                if(result.raw){measurement.width=result.raw->metadata.visibleWidth;measurement.height=result.raw->metadata.visibleHeight;}
                if(result.analysis){measurement.analysisIdentity=result.analysis->identity;measurement.stageSeconds=result.analysis->stageSeconds;measurement.diagnostics=result.analysis->diagnostics;}
                batch.measurements.push_back(std::move(measurement));
                if(canceled()||result.status==Raw::Bracketing::BracketingResult::Status::Canceled){batch.canceled=true;break;}
                if(result.status!=Raw::Bracketing::BracketingResult::Status::Completed||!result.raw)throw std::runtime_error(result.message);
                auto destination=set.parentDirectory;
                if(options.location==OutputLocation::NewFolder){
                    auto found=folders.find(destination);
                    if(found==folders.end())found=folders.emplace(destination,NewFolder(destination)).first;
                    destination=found->second;
                }
                const auto outputStart=std::chrono::steady_clock::now();
                bool complete=true;std::set<Output::Format> written;
                for(std::size_t f=0;f<options.formats.size();++f){
                    if(canceled()){batch.canceled=true;complete=false;break;}
                    const auto format=options.formats[f];if(!written.insert(format).second)continue;
                    Output::Request output;output.format=format;output.memoryBudgetBytes=budget.budgetBytes;
                    output.destination=destination/(set.sources.front().path.stem().wstring()+L"_burst"+TextPath(Output::Extension(format)).wstring());
                    output.shouldCancel=canceled;
                    output.reportProgress=[&](double fraction,const std::string& detail){publish(.85+.15*(double(f)+fraction)/double(options.formats.size()),detail);};
                    auto saved=Output::Write(result,output);
                    if(saved.status==Output::Result::Status::Canceled){batch.canceled=true;complete=false;break;}
                    if(saved.status==Output::Result::Status::Completed)batch.outputs.push_back(std::move(saved.path));
                    else {complete=false;batch.failures.push_back({set.name,saved.message});}
                }
                batch.measurements.back().outputSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-outputStart).count();
                if(complete)++batch.completedSets;
                publish(1,batch.canceled?"Canceled":"Set finished");
                if(batch.canceled)break;
            }catch(const std::exception& error){batch.failures.push_back({set.name,error.what()});}
        }
        // Only remove empty directories reserved by this invocation; preserve
        // every completed output even if later work failed or was canceled.
        for(const auto& pair:folders){std::error_code error;std::filesystem::remove(pair.second,error);}
    }catch(const std::exception& error){batch.failures.push_back({"Batch",error.what()});}
    return batch;
}
}
