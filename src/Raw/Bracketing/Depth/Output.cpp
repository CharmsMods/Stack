#include "Depth.h"
#include "../ProcessingInternal.h"
#include "../ResultDetail.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>

namespace Raw::Bracketing::Depth {
Preview ReadResultDetail(const ProcessingRequest& request,const BracketingResult& result,unsigned x,unsigned y,unsigned size) {
    return ReadCompletedResultDetail(request,result,x,y,size);
}
bool Finish(const ProcessingRequest& request,BracketingResult& result,std::string& error) {
    if(!result.raw||!result.analysis||!result.analysis->depth)return true;
    const auto& measured=*result.analysis->depth;
    if(!measured.Available()) {
        result.raw->reconstructionDepth=result.analysis->depth;
        result.message+=" "+measured.message;
        return true;
    }
    auto depth=std::make_shared<ReconstructionDepth>(measured);
    if(request.reportProgress) request.reportProgress(.98,"Separating parallax masks");
    try {depth->masks=ExtractMasks(*depth,*result.raw,request.shouldCancel);}
    catch(const std::exception& e){
        depth->message+=" Depth masks unavailable: "+std::string(e.what());
        result.message+=" "+depth->message;result.raw->reconstructionDepth=std::move(depth);
        if (request.shouldCancel && request.shouldCancel()) { error="Canceled."; return false; }
        return true;
    }
    if(request.shouldCancel && request.shouldCancel()){error="Canceled.";return false;}
    const auto reference=std::find_if(request.sources.begin(),request.sources.end(),[&](const auto& source) {
        return source.frameId==request.recipe.originFrameId;
    });
    if (reference!=request.sources.end()) for (auto& mask : depth->masks)
        mask.id=Stack::NodeMath::Sha256ContentIdentity({reference->sha256,mask.id}).substr(7);
    depth->message+=depth->masks.empty() ? " No distinct supported depth regions." :
        " "+std::to_string(depth->masks.size())+" graph masks.";
    result.message+=" "+depth->message;
    result.raw->reconstructionDepth=std::move(depth);
    return true;
}
}
