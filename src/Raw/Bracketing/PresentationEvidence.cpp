#include "PresentationEvidence.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
void PublishEvidence(const ProcessingRequest& request,ProcessingStage stage,
    std::shared_ptr<const ProcessingEvidence> evidence,const std::string& capture) try {
    if(!request.reportPresentation)return;
    ProcessingProgress event;event.stage=stage;event.captureId=capture;
    event.pass=request.presentationPass;event.evidenceOnly=true;event.evidence=std::move(evidence);
    request.reportPresentation(event);
} catch(...) { /* Presentation failures must not change processing. */ }
EvidenceRaster MakeEvidenceRaster(const std::string& label,EvidenceKind kind,unsigned width,unsigned height,
    double rawWidth,double rawHeight,float minimum,float maximum) {
    EvidenceRaster out;out.label=label;out.kind=kind;out.width=width;out.height=height;
    out.rawWidth=rawWidth;out.rawHeight=rawHeight;out.minimum=minimum;out.maximum=maximum;
    out.rgba.resize(std::size_t(width)*height*4);return out;
}
void EvidenceColor(EvidenceRaster& out,std::size_t p,const std::array<double,3>& rgb,const RawMetadata& metadata) {
    bool valid=true;
    for(unsigned c=0;c<3;++c) {
        double v=0;
        for(unsigned k=0;k<3;++k)v+=metadata.cameraToSrgb[c*3+k]*rgb[k]*
            metadata.cameraWhiteBalance[k]/std::max(1e-6f,metadata.cameraWhiteBalance[1]);
        valid&=std::isfinite(v);v=std::isfinite(v)?std::max(0.,v):0;v/=1+v;
        v=v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;
        out.rgba[p*4+c]=std::uint8_t(std::clamp(v,0.,1.)*255+.5);
    }
    out.rgba[p*4+3]=valid?255:0;
}
void EvidenceScalar(EvidenceRaster& out,std::size_t p,float value,bool known) {
    known&=std::isfinite(value);
    const float v=known?std::clamp((value-out.minimum)/std::max(1e-9f,out.maximum-out.minimum),0.f,1.f):0;
    for(unsigned c=0;c<3;++c)out.rgba[p*4+c]=std::uint8_t(v*255+.5f);
    out.rgba[p*4+3]=known?255:0;
}
namespace {
EvidenceRaster Capture(const PreparedSource&,const RawMetadata&,double,const char*);
void PublishSource(const ProcessingRequest& request,ProcessingStage stage,
    std::shared_ptr<const ProcessingEvidence> evidence,const PreparedSource& source) {
    ProcessingProgress event;event.stage=stage;event.captureId=source.id;
    event.evidenceOnly=true;event.evidence=std::move(evidence);event.thumbnail=source.presentationThumbnail.lock();
    if(!event.thumbnail&&source.original) {
        auto raster=Capture(source,source.metadata,1,"Capture");
        auto thumbnail=std::make_shared<ProcessingThumbnail>();thumbnail->frameId=source.id;
        thumbnail->group=unsigned(source.group);thumbnail->width=raster.width;thumbnail->height=raster.height;
        thumbnail->rgba=std::move(raster.rgba);event.thumbnail=thumbnail;source.presentationThumbnail=thumbnail;
    }
    event.pass=request.presentationPass;request.reportPresentation(event);
}
EvidenceRaster Capture(const PreparedSource& source,const RawMetadata& display,double gain,const char* label) {
    const auto& input=*source.original;
    const unsigned step=std::max(1u,(std::max(input.width,input.height)+255)/256);
    auto out=MakeEvidenceRaster(label,EvidenceKind::Image,(input.width+step-1)/step,
        (input.height+step-1)/step,source.frame.activeExtent.width,source.frame.activeExtent.height);
    for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x) {
        const auto p=(std::size_t(y*step)*input.width+x*step)*3;
        EvidenceColor(out,std::size_t(y)*out.width+x,{input.rgb[p]*gain,input.rgb[p+1]*gain,input.rgb[p+2]*gain},display);
    }
    return out;
}
}
void ObserveAlignment(const ProcessingRequest& request,const PreparedDataset& data,const PreparedSource& source) try {
    if(!request.reportPresentation)return;
    auto e=std::make_shared<ProcessingEvidence>();
    const double w=std::max(1u,data.width),h=std::max(1u,data.height);
    const auto origin=source.alignment.Map({0,0});const auto& a=source.alignment.linear;
    e->affine={float(a[0]),float(a[1]*h/w),float(origin.x/w),float(a[2]*w/h),float(a[3]),float(origin.y/h)};
    e->hasAffine=true;e->caption="Reference coordinates";e->equation="p' = A p + t";
    const auto& d=source.alignmentDiagnostic;
    if(!d.accepted)e->caption="Uncertain registration. Testing fixed reference coordinates.";
    e->metrics={{"Horizontal","px",d.translationX},{"Vertical","px",d.translationY},
        {"Rotation","deg",d.rotationDegrees},{"Overlap","%",d.overlapFraction*100}};
    if(!d.accepted)e->metrics.clear();
    if(data.sources[data.origin].original)e->rasters.push_back(Capture(data.sources[data.origin],data.sources[data.origin].metadata,1,"Reference"));
    PublishSource(request,ProcessingStage::GlobalAlignment,e,source);
} catch(...) { /* Presentation failures must not change processing. */ }
void ObserveExposure(const ProcessingRequest& request,const PreparedDataset& data,const PreparedSource& source) try {
    if(!request.reportPresentation||!source.original)return;
    auto e=std::make_shared<ProcessingEvidence>();e->equation="I' = I x 2^(-EV)";
    e->rasters.push_back(Capture(source,data.sources[data.origin].metadata,source.scale,"Exposure normalized"));
    e->metrics={{"Relative exposure","EV",-std::log2(source.scale)},{"Linear gain","x",source.scale},
        {"Gain uncertainty","",std::sqrt(std::max(0.,source.scaleVariance))}};
    e->curveLabel="Capture exposure";e->xUnits="capture";e->yUnits="EV";
    for(std::size_t i=0;i<data.sources.size();++i)e->curve.push_back({float(i+1),float(-std::log2(data.sources[i].scale))});
    PublishSource(request,ProcessingStage::Exposure,e,source);
} catch(...) { /* Presentation failures must not change processing. */ }
void ObserveNoise(const ProcessingRequest& request,const PreparedSource& source,const std::vector<EvidencePoint>& observations) try {
    if(!request.reportPresentation)return;
    auto e=std::make_shared<ProcessingEvidence>();e->equation="variance = shot x signal + read + quantization";
    e->caption=Mfd::NoiseModelQualityName(source.noise.quality);
    e->curveLabel="Noise variance model";e->xUnits="signal";e->yUnits="variance";
    e->observations=observations;
    double shot=0,read=0;
    for(const auto& p:source.noise.sites) {shot+=p.shotScale*.25;read+=(p.offsetVariance+(p.quantizationIncluded?0:p.quantizationVariance))*.25;}
    if(!observations.empty()) {
        const auto& p=source.noise.sites[0];shot=p.shotScale;
        read=p.offsetVariance+(p.quantizationIncluded?0:p.quantizationVariance);
        e->curveLabel="Red-site noise measurements";
    }
    float maximum=1;
    if(!observations.empty()) {maximum=.01f;for(const auto& p:observations)maximum=std::max(maximum,p.x);}
    for(unsigned b=0;b<64;++b) {float x=maximum*b/63;e->curve.push_back({x,float(shot*x+read)});}
    e->metrics={{"Shot coefficient","",shot},{"Read + quantization","",read}};
    PublishSource(request,ProcessingStage::Noise,e,source);
} catch(...) { /* Presentation failures must not change processing. */ }
void ObserveMotion(const ProcessingRequest& request,const PreparedSource& source,const Mfd::LocalMotionGrid& grid,bool provisional) try {
    if(!request.reportPresentation)return;
    auto e=std::make_shared<ProcessingEvidence>();e->provisional=provisional;
    e->caption=provisional?"Searching correspondence":"Verified correspondence";
    const float w=float(std::max<std::uint64_t>(1,grid.referenceRawExtent.width));
    const float h=float(std::max<std::uint64_t>(1,grid.referenceRawExtent.height));
    const auto stride=std::max<std::size_t>(1,(grid.nodes.size()+383)/384);
    float largest=-1;
    for(std::size_t i=0;i<grid.nodes.size();i+=stride) {
        const auto& n=grid.nodes[i];const auto mapped=grid.globalWarp.Map(n.centerRaw);
        const auto dx=float(mapped.x+n.residualRaw.x-n.centerRaw.x),dy=float(mapped.y+n.residualRaw.y-n.centerRaw.y);
        if(!std::isfinite(dx)||!std::isfinite(dy))continue;
        e->vectors.push_back({float(n.centerRaw.x)/w,float(n.centerRaw.y)/h,dx/w,dy/h,float(n.confidence),n.state==Mfd::MotionNodeState::Rejected});
        if(!e->vectors.back().rejected&&dx*dx+dy*dy>largest) {largest=dx*dx+dy*dy;e->focusX=e->vectors.back().x;e->focusY=e->vectors.back().y;}
    }
    e->metrics={{"Sampled vectors","",double(e->vectors.size())}};
    PublishSource(request,ProcessingStage::LocalAlignment,e,source);
} catch(...) { /* Presentation failures must not change processing. */ }
void ObserveReliability(const ProcessingRequest& request,const PreparedSource& source,const Mfd::ReliabilityMap& map,bool provisional) try {
    if(!request.reportPresentation||map.cells.empty())return;
    auto e=std::make_shared<ProcessingEvidence>();e->provisional=provisional;
    e->caption=provisional?"Alignment confidence before color checks":"Confidence after color agreement";
    e->equation="w = alignment confidence x color agreement";
    const unsigned step=unsigned(std::max<std::uint64_t>(1,(std::max(map.cellExtent.width,map.cellExtent.height)+255)/256));
    const unsigned w=unsigned((map.cellExtent.width+step-1)/step),h=unsigned((map.cellExtent.height+step-1)/step);
    auto confidence=MakeEvidenceRaster("Confidence",EvidenceKind::Confidence,w,h,map.rawExtent.width,map.rawExtent.height);
    auto rejected=MakeEvidenceRaster("Rejected measurements",EvidenceKind::Rejection,w,h,map.rawExtent.width,map.rawExtent.height);
    std::size_t rejectedCount=0;float strongest=-1;
    constexpr unsigned hardBits=1|2|16|32;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const auto& cell=map.cells[std::size_t(y*step)*map.cellExtent.width+x*step];const auto p=std::size_t(y)*w+x;
        const float v=float(provisional?cell.alignmentConfidence:cell.reliability);const bool reject=(cell.rejectionBits&hardBits)!=0;
        EvidenceScalar(confidence,p,v);EvidenceScalar(rejected,p,reject?1.f:0.f);rejectedCount+=reject;
        const float interest=(reject?1.f:1-v)-.1f*std::hypot(float(x)/w-.5f,float(y)/h-.5f);
        if(interest>strongest){strongest=interest;e->focusX=(x+.5f)/w;e->focusY=(y+.5f)/h;}
    }
    e->rasters={std::move(confidence),std::move(rejected)};
    e->metrics={{"Rejected samples","%",100.*rejectedCount/(w*h)}};
    PublishSource(request,ProcessingStage::LocalAlignment,e,source);
} catch(...) { /* Presentation failures must not change processing. */ }
}
