#include "BurstAlignment.h"
#include "BurstGuide.h"
#include "CaptureMotion.h"
#include "CaptureReliability.h"
#include "CaptureCorrespondence.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
namespace {
bool NeedsBurstRefinement(const ProcessingRequest& request,const PreparedDataset& data) {
    return request.recipe.alignmentMode==AlignmentMode::AutomaticLocal&&
        std::count_if(data.sources.begin(),data.sources.end(),[](const auto& s){return s.enabled;})>=3;
}
struct RefinementMemoryPlan {
    unsigned width=0,height=0;
    std::size_t cacheTiles=0;
    std::uint64_t requiredBytes=0;
};
RefinementMemoryPlan PlanRefinementMemory(const PreparedDataset& data,unsigned spacing) {
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
    const long double workspace=std::max(static_cast<long double>(data.width)*data.height*72,cacheBytes);
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

bool PreflightBurstRefinement(const ProcessingRequest& request,const PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    if(!NeedsBurstRefinement(request,data))return true;
    // Local-motion nodes use twice this raw spacing, and burst refinement
    // samples halfway between them. Global-only fallbacks may be coarser;
    // admit the normal full-quality path before spending time on alignment.
    const auto spacing=std::max(8u,CaptureAlignmentParameters(data).registration.finestStridePlanePixels);
    const auto plan=PlanRefinementMemory(data,spacing);
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
    const auto plan=PlanRefinementMemory(data,spacing);
    if(!AdmitRefinement(request,plan,error))return false;
    const auto width=plan.width,height=plan.height;
    const std::uint64_t count=std::uint64_t(width)*height;
    if(request.reportProgress)request.reportProgress(.50,"Checking repeatable burst structure");
    std::vector<BurstGuidePoint> guide(static_cast<std::size_t>(count));
    std::size_t repeatable=0;
    { // The guide is immutable after this phase; its tile cache can retire.
    PreparedTileSampler sampler(data.directory,plan.cacheTiles);
    for(unsigned y=1;y+1<height;++y) {
      if(request.reportProgress&&y%8==1)request.reportProgress(.50,
          "Checking repeatable burst structure: row "+std::to_string(y)+" of "+std::to_string(height-2));
      for(unsigned x=1;x+1<width;++x) {
        if(!MeasureBurstGuide(request,data,sampler,x*spacing,y*spacing,guide[std::size_t(y)*width+x],error))return false;
        repeatable+=guide[std::size_t(y)*width+x].repeated;
      }
    }
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
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        if(request.reportProgress)request.reportProgress(.50+.02*double(index)/data.sources.size(),"Refining supported burst alignment");
        Mfd::CfaPlanePyramid alternate;
        if(!BuildCapturePyramid(source,data.directory,parameters,request.shouldCancel,alternate,error))return false;
        Mfd::BidirectionalLocalMotionRequest motion;
        motion.reference=&reference;motion.alternate=&alternate;motion.referenceToAlternate=source.alignment;
        motion.exposureScale=source.scale;motion.options.registration=parameters.registration;motion.shouldCancel=request.shouldCancel;
        auto field=std::make_shared<MotionRefinementField>();field->width=width;field->height=height;field->spacing=spacing;
        field->cells.resize(static_cast<std::size_t>(count));std::size_t accepted=0;
        for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x) {
            if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
            const auto p=std::size_t(y)*width+x;if(!guide[p].repeated)continue;
            const Mfd::RawCoordinate point={double(x*spacing),double(y*spacing)};
            if(EvaluateCaptureCorrespondence(source,point).confidence<.4)continue;
            Mfd::LocalMotionFieldSample original;Mfd::LocalMotionOptions options;
            if(!EvaluateCaptureMotion(source,point,options,original)||original.alignmentConfidence<.4)continue;
            Mfd::MotionNode refined;
            if(!Mfd::RefineBidirectionalMotionPoint(motion,point,original.sourceRaw,refined))continue;
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
    if(!identity.empty())data.motionIdentity=Stack::NodeMath::Sha256ContentIdentity(identity);
    return true;
}
}
