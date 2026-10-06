#include "BurstAlignment.h"
#include "BurstGuide.h"
#include "CaptureMotion.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
bool RefineBurstAlignment(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    if(request.recipe.alignmentMode!=AlignmentMode::AutomaticLocal||
       std::count_if(data.sources.begin(),data.sources.end(),[](const auto& s){return s.enabled;})<3)return true;
    ProcessingTimer timer(data.stageSeconds,"burst_alignment_refinement");
    unsigned spacing=0;
    for(const auto& s:data.sources)if(s.enabled&&s.localMotion) {
        const unsigned half=std::max(8u,unsigned(s.localMotion->spacingRawX/2));
        spacing=spacing?std::min(spacing,half):half;
    }
    if(!spacing)return true;
    const unsigned width=(data.width+spacing-1)/spacing+1,height=(data.height+spacing-1)/spacing+1;
    const std::uint64_t count=std::uint64_t(width)*height;
    const std::uint64_t fieldBytes=count*(sizeof(BurstGuidePoint)+data.sources.size()*sizeof(MotionCorrection));
    // Existing local search already admits two full pyramids. Reserve the same
    // working space plus the persistent correction fields before allocating.
    if(fieldBytes>request.memoryBudgetBytes/16||
       std::uint64_t(data.width)*data.height*72+fieldBytes>request.memoryBudgetBytes/2) {
        diagnostics.push_back("Burst refinement retained existing motion within the memory budget.");return true;
    }
    if(request.reportProgress)request.reportProgress(.50,"Checking repeatable burst structure");
    std::vector<BurstGuidePoint> guide(static_cast<std::size_t>(count));
    PreparedTileSampler sampler(data.directory,8);std::size_t repeatable=0;
    for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x) {
        if(!MeasureBurstGuide(request,data,sampler,x*spacing,y*spacing,guide[std::size_t(y)*width+x],error))return false;
        repeatable+=guide[std::size_t(y)*width+x].repeated;
    }
    if(!repeatable) {
        diagnostics.push_back("Burst subsets supplied no repeatable structure for additional alignment.");return true;
    }
    Mfd::Parameters parameters;parameters.registration.finestPatchPlanePixels=16;
    Mfd::CfaPlanePyramid reference;
    if(!BuildCapturePyramid(data.sources[data.origin],data.directory,parameters,request.shouldCancel,reference,error))return false;
    std::vector<std::shared_ptr<MotionRefinementField>> pending(data.sources.size());
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
            Mfd::LocalMotionFieldSample original;Mfd::LocalMotionOptions options;
            if(!EvaluateCaptureMotion(source,point,options,original)||original.alignmentConfidence<.4)continue;
            Mfd::MotionNode refined;
            if(!Mfd::RefineBidirectionalMotionPoint(motion,point,original.sourceRaw,refined))continue;
            auto& correction=field->cells[p];
            correction.delta={refined.residualRaw.x-original.residualRaw.x,refined.residualRaw.y-original.residualRaw.y};
            correction.covariance=refined.covarianceRaw;correction.confidence=float(refined.confidence);++accepted;
        }
        if(accepted)pending[index]=std::move(field);
        diagnostics.push_back(source.id+": accepted "+std::to_string(accepted)+" burst-supported local refinements.");
    }
    std::string identity;
    const auto append=[&](const auto& value){identity.append(reinterpret_cast<const char*>(&value),sizeof(value));};
    for(std::size_t i=0;i<data.sources.size();++i) {
        data.sources[i].refinedMotion=std::move(pending[i]);
        const auto* field=data.sources[i].refinedMotion.get();if(!field)continue;
        identity+=data.sources[i].id;identity+='\0';append(field->width);append(field->height);append(field->spacing);
        for(const auto& cell:field->cells) {
            append(cell.delta.x);append(cell.delta.y);append(cell.confidence);
            append(cell.covariance.xxRawPixelsSquared);append(cell.covariance.xyRawPixelsSquared);append(cell.covariance.yyRawPixelsSquared);
        }
    }
    // A memory-limited run may keep the original field. Its group tiles must
    // not collide with another run that accepted additional refinements.
    if(!identity.empty())data.motionIdentity=Stack::NodeMath::Sha256ContentIdentity(identity);
    return true;
}
}
