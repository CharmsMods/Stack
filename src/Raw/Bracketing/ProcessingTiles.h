#pragma once

#include "ProcessingInternal.h"
#include "DetailFusion.h"
#include "LocalDetail.h"
#include "ParallelProcessing.h"
#include "PresentationEvidence.h"
#include <algorithm>
#include <mutex>

namespace Raw::Bracketing {
inline std::uint64_t GroupWorkerBytes(std::size_t groups,unsigned tilePixels,std::size_t captures,
    std::uint64_t memoryBudgetBytes=std::numeric_limits<std::uint64_t>::max()) {
    // Include the temporal detail halo, band accumulators, a source observation
    // buffer, output/canonicalization copies, and the two live tile samplers.
    const std::uint64_t side=static_cast<std::uint64_t>(tilePixels)+8;
    return side*side*(groups*(4*sizeof(Observation)+sizeof(DetailFusionAccumulator))+224)+
        std::max<std::uint64_t>(64ull*1024*1024,LocalDetailWorkingBytes(captures,tilePixels,memoryBudgetBytes));
}

template<class Task>
bool ProcessTiles(const ProcessingRequest& request,const PreparedDataset& data,
    double progressStart,double progressSpan,const char* progressLabel,ProcessingStage stage,
    Task&& task,std::string& error) {
    ReportPresentation(request,stage);
    if(request.evidenceStream)request.evidenceStream->Begin(request,stage,data.width,data.height,data.sources[data.origin].metadata);
    const auto& origin=data.sources[data.origin].frame;
    const std::size_t total=static_cast<std::size_t>(origin.tileColumns)*origin.tileRows;
    if(!total) {error="The prepared bracket contains no output tiles.";return false;}
    const std::uint64_t perWorker=GroupWorkerBytes(request.recipe.groups.size(),origin.tileRawPixels,
        data.sources.size(),request.memoryBudgetBytes);
    const std::uint64_t reserve=static_cast<std::uint64_t>(data.width)*data.height*36+64ull*1024*1024;
    if(request.memoryBudgetBytes<=reserve||request.memoryBudgetBytes-reserve<perWorker) {
        error="The output and group-processing buffers exceed the available memory budget.";return false;
    }
    const auto workers=std::min<std::size_t>({total,
        ResolveBracketingWorkerCount(request.memoryBudgetBytes,request.workerCount),
        static_cast<std::size_t>((request.memoryBudgetBytes-reserve)/perWorker)});
    std::size_t completed=0;
    std::mutex errorMutex,progressMutex;
    const bool success=ProcessParallelIndices(total,workers,request.shouldCancel,[&](std::size_t index) {
        const auto tx=static_cast<unsigned>(index%origin.tileColumns);
        const auto ty=static_cast<unsigned>(index/origin.tileColumns);
        std::string tileError;
        if(!task(tx,ty,tileError)) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if(error.empty()) error=tileError.empty()?"Bracketing tile processing failed.":tileError;
            return false;
        }
        {
            // Increment and publish under the same lock: a later tile cannot
            // publish progress before an earlier completion reports its count.
            std::lock_guard<std::mutex> lock(progressMutex);
            const auto done=++completed;
            ReportPresentation(request,stage,done,total);
            if(request.reportProgress)
                request.reportProgress(progressStart+progressSpan*done/total,progressLabel);
        }
        return true;
    });
    if(request.evidenceStream)request.evidenceStream->Flush(request);
    if(!success&&error.empty()) error="Canceled";
    return success;
}
} // namespace Raw::Bracketing
