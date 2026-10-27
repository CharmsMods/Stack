#include "BurstAlignment.h"
#include "BurstGuide.h"
#include "CaptureMotion.h"
#include "CaptureReliability.h"
#include "CaptureCorrespondence.h"
#include "PhotometricReliability.h"
#include "ParallelProcessing.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace Raw::Bracketing {
namespace {
bool NeedsBurstRefinement(const ProcessingRequest& request,const PreparedDataset& data) {
    return request.recipe.alignmentMode==AlignmentMode::AutomaticLocal&&
        std::count_if(data.sources.begin(),data.sources.end(),[](const auto& s){return s.enabled;})>=3;
}
struct RefinementMemoryPlan {
    unsigned width=0,height=0;
    std::size_t cacheTiles=0;
    std::size_t guideWorkers=1;
    std::uint64_t requiredBytes=0;
};
RefinementMemoryPlan PlanRefinementMemory(const ProcessingRequest& request,const PreparedDataset& data,unsigned spacing) {
    RefinementMemoryPlan plan;
    plan.width=static_cast<unsigned>((std::uint64_t(data.width)+spacing-1)/spacing+1);
    plan.height=static_cast<unsigned>((std::uint64_t(data.height)+spacing-1)/spacing+1);
    const long double count=static_cast<long double>(plan.width)*plan.height;
    const long double fieldBytes=count*(sizeof(BurstGuidePoint)+
        static_cast<long double>(data.sources.size())*sizeof(MotionCorrection));
    const std::uint64_t stride=std::uint64_t(data.width)*data.height>=2'000'000?4:2;
    const long double reliabilityCells=((data.width+stride-1)/stride)*((data.height+stride-1)/stride);
    // Staged stores/inspections coexist with the originals. A single capture
    // also needs transient residual evidence and the full reliability map.
    const long double reliabilityBytes=reliabilityCells*(
        static_cast<long double>(data.sources.size())*20+sizeof(Mfd::ReliabilityCell)+24);
    // A guide visits every capture at each scene point. Retain each capture's
    // four-tile interpolation footprint within the same processing budget.
    const auto enabled=std::count_if(data.sources.begin(),data.sources.end(),[](const auto& s){return s.enabled;});
    plan.cacheTiles=std::size_t(enabled)*4+4;
    const long double tileSide=data.sources[data.origin].frame.tileRawPixels;
    const long double cacheBytes=(plan.cacheTiles+1)*(tileSide*tileSide*9+1024);
    // Guide tiles are released before pyramids are allocated. Original and
    // pending reliability/inspection payloads are both charged per capture.
    const long double guideWorkerBytes=cacheBytes+256ull*1024+
        static_cast<long double>(data.sources.size())*4096;
    const long double workspace=std::max(static_cast<long double>(data.width)*data.height*72,guideWorkerBytes);
    // Guide points share immutable captures and write disjoint cells. Their
    // private caches fit inside the already-admitted later pyramid workspace;
    // this does not multiply the request's peak memory allowance.
    plan.guideWorkers=std::min<std::size_t>({
        ResolveBracketingWorkerCount(request.memoryBudgetBytes,request.workerCount),
        std::max(1u,plan.height>2?plan.height-2:0u),
        static_cast<std::size_t>(workspace/guideWorkerBytes)});
    const long double requiredBytes=workspace+fieldBytes+reliabilityBytes+64ull*1024*1024;
    plan.requiredBytes=requiredBytes>=std::numeric_limits<std::uint64_t>::max()
        ?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(requiredBytes);
    return plan;
}
std::string MiB(std::uint64_t bytes) {
    constexpr std::uint64_t unit=1024ull*1024;
    return std::to_string(bytes/unit+(bytes%unit!=0));
}
bool AdmitRefinement(const ProcessingRequest& request,const RefinementMemoryPlan& plan,std::string& error) {
    if(plan.requiredBytes<=request.memoryBudgetBytes)return true;
    error="Full-quality burst alignment needs an estimated "+MiB(plan.requiredBytes)+
        " MiB; the current memory budget is "+MiB(request.memoryBudgetBytes)+
        " MiB. Close other memory-heavy work and retry; alignment quality was not reduced.";
    return false;
}
}

bool AdmitLocalAlignmentMemory(const ProcessingRequest& request,const PreparedDataset& data,
    std::uint64_t& requiredBytes,std::string& error) {
    requiredBytes=0;
    if(request.recipe.alignmentMode!=AlignmentMode::AutomaticLocal)return true;
    if(data.sources.empty()||data.origin>=data.sources.size()) {
        error="Local-alignment memory planning has no fixed reference.";return false;
    }
    const auto& reference=data.sources[data.origin];
    const bool hasAlternate=std::any_of(data.sources.begin(),data.sources.end(),
        [&](const auto& source){return &source!=&reference&&source.enabled;});
    if(!hasAlternate)return true;
    if(data.width<2||data.height<2||data.width%2||data.height%2) {
        error="Local-alignment memory planning requires an even Bayer extent.";return false;
    }
    bool representable=true;
    const auto add=[&](std::uint64_t a,std::uint64_t b) {
        if(a>std::numeric_limits<std::uint64_t>::max()-b){representable=false;return std::uint64_t(0);}
        return a+b;
    };
    const auto multiply=[&](std::uint64_t a,std::uint64_t b) {
        if(b&&a>std::numeric_limits<std::uint64_t>::max()/b){representable=false;return std::uint64_t(0);}
        return a*b;
    };
    const auto pixels=multiply(data.width,data.height);
    const auto parameters=CaptureAlignmentParameters(data);
    std::uint64_t planeWidth=data.width/2,planeHeight=data.height/2,levelSamples=0;
    for(unsigned level=0;level<parameters.registration.pyramidLevels;++level) {
        levelSamples=add(levelSamples,multiply(4,multiply(planeWidth,planeHeight)));
        planeWidth=planeWidth/2+planeWidth%2;planeHeight=planeHeight/2+planeHeight%2;
    }
    // Moved level-zero buffers are not duplicated. Clipped masks exist only
    // at level zero; the other 17 bytes/sample exist at every pyramid level.
    const auto pairPyramids=multiply(2,add(multiply(levelSamples,2*sizeof(double)+1),pixels));
    const auto stride=CaptureReliabilityStrideBayerCells({data.width,data.height});
    const std::uint64_t cellWidth=(std::uint64_t(data.width/2)+stride-1)/stride;
    const std::uint64_t cellHeight=(std::uint64_t(data.height/2)+stride-1)/stride;
    const auto cells=multiply(cellWidth,cellHeight);
    std::uint64_t colorWorking=0;
    if(!ColorReliabilityWorkingBytes(cells,colorWorking))representable=false;
    // Evidence retires before the compact store and inspection are published.
    // Old stores/inspections may remain live until a confidence refresh commits.
    constexpr std::uint64_t inspectionBytes=sizeof(float)+sizeof(std::uint16_t);
    constexpr std::uint64_t publishedCellBytes=inspectionBytes+2*sizeof(std::uint16_t);
    const auto retained=add(multiply(pixels/4,inspectionBytes),
        multiply(multiply(cells,publishedCellBytes),data.sources.size()-1));
    const auto reliabilityWorking=std::max(colorWorking,
        multiply(cells,sizeof(Mfd::ReliabilityCell)+publishedCellBytes));
    std::uint64_t tilePixels=0;
    for(const auto& source:data.sources) {
        if(!source.enabled && &source!=&reference)continue;
        if(source.frame.activeExtent.width!=data.width||source.frame.activeExtent.height!=data.height) {
            error="Local-alignment memory planning has mismatched prepared extents.";return false;
        }
        const auto side=source.frame.tileRawPixels;
        if(!side||side>16384) {
            error="Local-alignment memory planning has an invalid prepared tile size.";return false;
        }
        tilePixels=std::max(tilePixels,multiply(std::min(side,data.width),std::min(side,data.height)));
    }
    const auto tileBytes=multiply(tilePixels,2*sizeof(float)+1);
    // Retain the existing full-quality 72N search envelope, with explicit
    // ceil-rounded pyramid/staging and per-worker allowances for unusual shapes.
    // This is host admission only: driver/VRAM/unified-memory costs remain
    // subject to the GPU owner's allocation checks, not a combined peak claim.
    const auto searchWorking=std::max(multiply(pixels,72),
        add(pairPyramids,add(pixels,multiply(ResolveBracketingWorkerCount(
            request.memoryBudgetBytes,request.workerCount),512ull*1024))));
    const auto working=std::max({add(pairPyramids,tileBytes),searchWorking,
        add(pairPyramids,reliabilityWorking),add(pairPyramids/2,multiply(tileBytes,9))});
    // The bounded original overviews, motion fields, metadata, vector/thread
    // overhead and small radiometric scratch share the existing 64 MiB margin.
    // Decoder images, group workers and final output are not live in this phase.
    requiredBytes=add(add(retained,working),64ull*1024*1024);
    if(!representable) {
        requiredBytes=0;error="Local-alignment memory estimate overflowed.";return false;
    }
    if(requiredBytes<=request.memoryBudgetBytes)return true;
    error="Full-quality local alignment needs an estimated "+MiB(requiredBytes)+
        " MiB of host memory; the current memory budget is "+MiB(request.memoryBudgetBytes)+
        " MiB. Close other memory-heavy work and retry; alignment quality was not reduced.";
    return false;
}

bool PreflightBurstRefinement(const ProcessingRequest& request,const PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    std::uint64_t localBytes=0;
    if(!AdmitLocalAlignmentMemory(request,data,localBytes,error))return false;
    if(localBytes)diagnostics.push_back("Full-quality local alignment preflight: estimated "+
        MiB(localBytes)+" MiB of host memory; memory budget "+MiB(request.memoryBudgetBytes)+" MiB.");
    if(!NeedsBurstRefinement(request,data))return true;
    // Local-motion nodes use twice this raw spacing, and burst refinement
    // samples halfway between them. Global-only fallbacks may be coarser;
    // admit the normal full-quality path before spending time on alignment.
    const auto spacing=std::max(8u,CaptureAlignmentParameters(data).registration.finestStridePlanePixels);
    const auto plan=PlanRefinementMemory(request,data,spacing);
    if(!AdmitRefinement(request,plan,error))return false;
    diagnostics.push_back("Full-quality burst alignment preflight: estimated "+
        MiB(plan.requiredBytes)+" MiB; memory budget "+MiB(request.memoryBudgetBytes)+" MiB.");
    return true;
}

bool RefineBurstAlignment(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    if(!NeedsBurstRefinement(request,data))return true;
    ProcessingTimer timer(data.stageSeconds,"burst_alignment_refinement");
    unsigned spacing=0;
    for(const auto& s:data.sources)if(s.enabled&&s.localMotion) {
        const unsigned half=std::max(8u,unsigned(s.localMotion->spacingRawX/2));
        spacing=spacing?std::min(spacing,half):half;
    }
    if(!spacing)return true;
    const auto plan=PlanRefinementMemory(request,data,spacing);
    if(!AdmitRefinement(request,plan,error))return false;
    const auto width=plan.width,height=plan.height;
    const std::uint64_t count=std::uint64_t(width)*height;
    if(request.reportProgress)request.reportProgress(.50,"Checking repeatable burst structure");
    std::vector<BurstGuidePoint> guide(static_cast<std::size_t>(count));
    std::size_t repeatable=0;
    { // The guide is immutable after this phase; its tile cache can retire.
    ProcessingTimer guideTimer(data.stageSeconds,"burst_guide");
    std::vector<std::size_t> repeated(plan.guideWorkers);
    std::atomic<bool> stopped{false};
    std::mutex errorMutex,progressMutex;
    unsigned completedRows=0;
    const auto rows=height>2?height-2:0;
    const bool success=ProcessParallelIndices(plan.guideWorkers,plan.guideWorkers,request.shouldCancel,
        [&](std::size_t worker) {
            PreparedTileSampler sampler(data.directory,plan.cacheTiles);
            std::string workerError;
            const auto first=1+std::size_t(rows)*worker/plan.guideWorkers;
            const auto end=1+std::size_t(rows)*(worker+1)/plan.guideWorkers;
            for(auto y=first;y<end;++y) {
                for(unsigned x=1;x+1<width;++x) {
                    if(stopped.load(std::memory_order_relaxed))return false;
                    auto& point=guide[y*width+x];
                    if(!MeasureBurstGuide(request,data,sampler,x*spacing,unsigned(y)*spacing,point,workerError)) {
                        stopped.store(true,std::memory_order_relaxed);
                        std::lock_guard<std::mutex> lock(errorMutex);
                        if(error.empty())error=workerError.empty()?"Burst structure measurement failed.":workerError;
                        return false;
                    }
                    repeated[worker]+=point.repeated;
                }
                if(request.reportProgress) {
                    std::lock_guard<std::mutex> lock(progressMutex);
                    if(++completedRows%8==0||completedRows==rows)
                        request.reportProgress(.50,"Checking repeatable burst structure: row "+
                            std::to_string(completedRows)+" of "+std::to_string(rows));
                }
            }
            return true;
        });
    if(!success) {if(error.empty())error="Canceled";return false;}
    for(const auto count:repeated)repeatable+=count;
    }
    if(!repeatable) {
        diagnostics.push_back("Burst subsets supplied no repeatable structure for additional alignment.");return true;
    }
    auto parameters=CaptureAlignmentParameters(data);parameters.registration.finestPatchPlanePixels=16;
    Mfd::CfaPlanePyramid reference;
    if(!BuildCapturePyramid(data.sources[data.origin],data.directory,parameters,request.shouldCancel,reference,error))return false;
    std::vector<std::unique_ptr<PreparedSource>> pending(data.sources.size());
    // All candidates see the same frozen initial fields and subset guide.
    for(std::size_t index=0;index<data.sources.size();++index) {
        const auto& source=data.sources[index];if(index==data.origin||!source.enabled||!source.localMotion)continue;
        // Do not independently pull a brighter group's anchor or repeats back
        // toward a reference that cannot observe their shadow structure.
        if(source.exposureGroupMotion)continue;
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        if(request.reportProgress)request.reportProgress(.50+.02*double(index)/data.sources.size(),"Refining supported burst alignment");
        Mfd::CfaPlanePyramid alternate;
        if(!BuildCapturePyramid(source,data.directory,parameters,request.shouldCancel,alternate,error))return false;
        Mfd::BidirectionalLocalMotionRequest motion;
        motion.reference=&reference;motion.alternate=&alternate;motion.referenceToAlternate=source.alignment;
        motion.exposureScale=source.scale;motion.options.registration=parameters.registration;motion.shouldCancel=request.shouldCancel;
        std::shared_ptr<MotionRefinementField> field;std::size_t accepted=0;
        for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x) {
            if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
            const auto p=std::size_t(y)*width+x;if(!guide[p].repeated)continue;
            const Mfd::RawCoordinate point={double(x*spacing),double(y*spacing)};
            Mfd::LocalMotionFieldSample original;
            if(EvaluateCaptureCorrespondence(source,point,false,&original).confidence<.4||
               original.alignmentConfidence<.4)continue;
            Mfd::MotionNode refined;
            if(!Mfd::RefineBidirectionalMotionPoint(motion,point,original.sourceRaw,refined))continue;
            if(!field) {
                field=std::make_shared<MotionRefinementField>();
                field->width=width;field->height=height;field->spacing=spacing;
                field->cells.resize(static_cast<std::size_t>(count));
            }
            auto& correction=field->cells[p];
            correction.delta={refined.residualRaw.x-original.residualRaw.x,refined.residualRaw.y-original.residualRaw.y};
            correction.covariance=refined.covarianceRaw;correction.confidence=float(refined.confidence);++accepted;
        }
        if(accepted) {
            auto candidate=std::make_unique<PreparedSource>(source);
            candidate->refinedMotion=std::move(field);++candidate->motionRevision;
            // Reuse the two resident pyramids. Measure every reliability site
            // against the corrected warp before any candidate is published.
            std::string refreshError;
            if(!RefreshCaptureReliability(request,data.sources[data.origin],*candidate,parameters,
                reference,alternate,{},refreshError)) {
                if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
                error=source.id+": could not validate refined alignment: "+refreshError;
                return false;
            }
            pending[index]=std::move(candidate);
        }
        diagnostics.push_back(source.id+": accepted "+std::to_string(accepted)+" burst-supported local refinements.");
    }
    std::string identity;
    const auto append=[&](const auto& value){identity.append(reinterpret_cast<const char*>(&value),sizeof(value));};
    for(std::size_t i=0;i<data.sources.size();++i) {
        if(pending[i])data.sources[i]=std::move(*pending[i]);
        const auto* field=data.sources[i].refinedMotion.get();if(!field)continue;
        identity+=data.sources[i].id;identity+='\0';append(field->width);append(field->height);append(field->spacing);
        for(const auto& cell:field->cells) {
            append(cell.delta.x);append(cell.delta.y);append(cell.confidence);
            append(cell.covariance.xxRawPixelsSquared);append(cell.covariance.xyRawPixelsSquared);append(cell.covariance.yyRawPixelsSquared);
        }
    }
    // Only evidence-supported refinements are accepted. Preserve their identity
    // so cached group tiles and final measurements describe the actual warp.
    if(!identity.empty())data.motionIdentity=Stack::NodeMath::Sha256ContentIdentity(data.motionIdentity+identity);
    return true;
}
}
