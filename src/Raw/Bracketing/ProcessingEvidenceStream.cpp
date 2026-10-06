#include "PresentationEvidence.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
std::size_t PresentationGroup(const ProcessingRequest& request) {
    for(std::size_t i=0;i<request.recipe.groups.size();++i) {
        const auto& group=request.recipe.groups[i];
        if(group.enabled&&std::any_of(group.frames.begin(),group.frames.end(),[](const auto& f){return f.enabled;}))return i;
    }
    return 0;
}
void ProcessingEvidenceStream::Begin(const ProcessingRequest& request,ProcessingStage stage,unsigned width,unsigned height,const RawMetadata& metadata) try {
    std::lock_guard<std::mutex> lock(mutex_);
    stage_=stage;rawWidth_=width;rawHeight_=height;metadata_=metadata;
    step_=std::max(2u,((std::max(width,height)+255)/256+1)/2*2);
    const auto w=(width+step_-1)/step_,h=(height+step_-1)/step_;evidence_={};
    evidence_.rasters.push_back(MakeEvidenceRaster("Evaluated image",EvidenceKind::Image,w,h,width,height));
    if(stage==ProcessingStage::Guide) {
        evidence_.rasters[0].label="Neutral luminance";
        evidence_.rasters.push_back(MakeEvidenceRaster("Luminance guide",EvidenceKind::Guide,w,h,width,height,-12,8));
        evidence_.rasters.back().units="EV";evidence_.equation="EV = log2(Y / 0.18)";
    }else if(stage==ProcessingStage::Blend||stage==ProcessingStage::Reconstruction) {
        const auto group="Group "+std::to_string(PresentationGroup(request)+1);
        evidence_.rasters.push_back(MakeEvidenceRaster(group+" contribution",EvidenceKind::Contribution,w,h,width,height));
        evidence_.rasters.push_back(MakeEvidenceRaster(group+" requested",EvidenceKind::Contribution,w,h,width,height));
        evidence_.equation="I = sum(w x I) / sum(w)";
    }else {
        const auto g=PresentationGroup(request);
        const auto support=g<request.recipe.groups.size()?std::count_if(request.recipe.groups[g].frames.begin(),request.recipe.groups[g].frames.end(),[](const auto& f){return f.enabled;}):1;
        evidence_.rasters[0].label="Group "+std::to_string(g+1)+" accumulation";
        evidence_.rasters.push_back(MakeEvidenceRaster("Effective support",EvidenceKind::Support,w,h,width,height,0,float(std::max<std::ptrdiff_t>(1,support))));
        evidence_.equation="w = 1 / variance";
    }
    last_={};
} catch(...) { /* Presentation failures must not change processing. */ }
void ProcessingEvidenceStream::Tile(const ProcessingRequest& request,ProcessingStage stage,unsigned x,unsigned y,unsigned w,unsigned h,
    const std::function<PresentationSample(unsigned,unsigned)>& read) try {
    if(!request.reportPresentation)return;
    std::lock_guard<std::mutex> lock(mutex_);if(stage!=stage_||evidence_.rasters.size()<2)return;
    const unsigned width=evidence_.rasters[0].width;
    for(unsigned sy=(y+step_-1)/step_*step_;sy<y+h;sy+=step_)
        for(unsigned sx=(x+step_-1)/step_*step_;sx<x+w;sx+=step_) {
            const auto sample=read(sx,sy);const auto p=std::size_t(sy/step_)*width+sx/step_;
            EvidenceColor(evidence_.rasters[0],p,sample.rgb,metadata_);
            if(stage==ProcessingStage::Guide) {
                auto& r=evidence_.rasters[0];const double v=std::max(0.,sample.rgb[0]);
                const double mapped=v/(1+v);const auto gray=std::uint8_t(std::clamp(std::pow(mapped,1/2.4),0.,1.)*255+.5);
                r.rgba[p*4]=r.rgba[p*4+1]=r.rgba[p*4+2]=gray;
            }
            if(!sample.known)evidence_.rasters[0].rgba[p*4+3]=0;
            EvidenceScalar(evidence_.rasters[1],p,sample.first,sample.known);
            if(evidence_.rasters.size()>2)EvidenceScalar(evidence_.rasters[2],p,sample.second,sample.known);
        }
    const auto now=std::chrono::steady_clock::now();
    if(now-last_>=std::chrono::milliseconds(100)) {
        last_=now;PublishEvidence(request,stage_,std::make_shared<ProcessingEvidence>(evidence_));
    }
} catch(...) { /* Presentation failures must not change processing. */ }
void ProcessingEvidenceStream::Flush(const ProcessingRequest& request) try {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!evidence_.rasters.empty())PublishEvidence(request,stage_,std::make_shared<ProcessingEvidence>(evidence_));
} catch(...) { /* Presentation failures must not change processing. */ }
void ProcessingEvidenceStream::Details(const ProcessingEvidence& details) try {
    std::lock_guard<std::mutex> lock(mutex_);
    evidence_.kernels=details.kernels;evidence_.metrics=details.metrics;
    evidence_.caption=details.caption;
} catch(...) { /* Presentation failures must not change processing. */ }
}
