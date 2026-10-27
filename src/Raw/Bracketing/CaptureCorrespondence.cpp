#include "CaptureCorrespondence.h"
#include "ProcessingInternal.h"
#include "Raw/MultiFrameDenoise/MotionJacobian.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
bool HardCaptureRejection(std::uint16_t bits) {
    constexpr auto mask=Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::AlignmentInvalid)|
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::SampleInvalid)|
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::PatchRejected)|
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::NonFiniteEvidence);
    return (bits&mask)!=0;
}
void StampCaptureReliability(PreparedSource& source) {
    source.reliabilityMotionRevision=source.motionRevision;
    source.reliabilityScale=source.scale;
    source.reliabilityScaleVariance=source.scaleVariance;
}
bool HasCurrentCaptureReliability(const PreparedSource& source) {
    return source.reliability&&source.reliabilityMotionRevision==source.motionRevision&&
        source.reliabilityScale==source.scale&&source.reliabilityScaleVariance==source.scaleVariance;
}
bool BuildCaptureSupportMask(const PreparedSource& source,Mfd::RawCoordinate first,
    Mfd::RawCoordinate last,bool reference,CaptureSupportMask& mask,
    const std::function<bool()>& cancel,std::string& error) {
    // Clear every semantic field while retaining bounded scratch capacity.
    mask.mode=CaptureSupportMask::Mode::NotRequired;mask.width=mask.height=mask.cellStepRawPixels=0;
    mask.originRawX=mask.originRawY=mask.rawWidth=mask.rawHeight=0;mask.cells.clear();
    if(!std::isfinite(first.x)||!std::isfinite(first.y)||!std::isfinite(last.x)||!std::isfinite(last.y)||
       first.x>last.x||first.y>last.y) {error="Invalid capture-support crop.";return false;}
    if(cancel&&cancel()){error="Canceled";return false;}
    if(reference||!source.localMotion)return true;
    mask.mode=CaptureSupportMask::Mode::AllRejected;
    if(!HasCurrentCaptureReliability(source))return true;
    Mfd::ReliabilityRawGridGeometry geometry;
    if(!Mfd::ReadReliabilityStoreRawGridGeometry(*source.reliability,geometry))return true;
    if(last.x<0||last.y<0||first.x>=double(geometry.rawExtent.width)||
       first.y>=double(geometry.rawExtent.height))return true;
    const auto step=geometry.cellStepRawPixels;
    const auto cell=[&](double p,std::uint64_t count) {
        return static_cast<std::uint32_t>(std::min(double(count-1),std::floor(std::max(0.,p)/step)));
    };
    const auto x0=cell(first.x,geometry.cellExtent.width),y0=cell(first.y,geometry.cellExtent.height);
    const auto x1=cell(last.x,geometry.cellExtent.width),y1=cell(last.y,geometry.cellExtent.height);
    const auto width=std::uint64_t(x1)-x0+1,height=std::uint64_t(y1)-y0+1;
    if(width>std::numeric_limits<std::uint32_t>::max()||height>std::numeric_limits<std::uint32_t>::max()||
       width>mask.cells.max_size()/height) {error="Capture-support crop exceeds addressable storage.";return false;}
    mask.cells.resize(std::size_t(width*height));
    for(std::uint32_t y=0;y<height;++y) {
        if(cancel&&cancel()){error="Canceled";return false;}
        for(std::uint32_t x=0;x<width;++x) {
            double reliability=0;std::uint16_t bits=0;
            const bool supported=Mfd::ReadReliabilityStoreCell(*source.reliability,x0+x,y0+y,
                reliability,&bits)&&std::isfinite(reliability)&&!HardCaptureRejection(bits);
            mask.cells[std::size_t(y)*width+x]=supported?1.f:0.f;
        }
    }
    mask.width=std::uint32_t(width);mask.height=std::uint32_t(height);mask.cellStepRawPixels=step;
    mask.originRawX=std::uint64_t(x0)*step;mask.originRawY=std::uint64_t(y0)*step;
    mask.rawWidth=geometry.rawExtent.width;mask.rawHeight=geometry.rawExtent.height;
    mask.mode=CaptureSupportMask::Mode::Raster;
    return true;
}
CaptureCorrespondence EvaluateCaptureCorrespondence(const PreparedSource& source,
    Mfd::RawCoordinate point,bool reference,Mfd::LocalMotionFieldSample* evaluatedMotion,Mfd::RawMappingJacobian* jacobian) {
    if(jacobian)*jacobian={};
    if(reference){if(jacobian)jacobian->valid=true;return {point,{},1};}
    const double floor=source.alignmentDiagnostic.model==CaptureAlignment::Model::Identity?0:.0025;
    const auto global=[&](double confidence) {
        if(jacobian){jacobian->linear=source.alignment.linear;jacobian->valid=Mfd::FiniteMappingJacobian(*jacobian);}
        return CaptureCorrespondence{source.alignment.Map(point),{floor,0,floor},confidence};
    };
    if(!std::isfinite(point.x)||!std::isfinite(point.y))return global(0);
    CaptureCorrespondence result;
    if(source.localMotion) {
        Mfd::LocalMotionFieldSample motion;double refinementConfidence=1;
        if(!EvaluateCaptureMotion(source,point,{},motion,&refinementConfidence,jacobian)) {
            return global(0);
        }
        if(evaluatedMotion)*evaluatedMotion=motion;
        result.sourceRaw=motion.sourceRaw;result.covarianceRaw=motion.covarianceRaw;
        result.confidence=0;
        // A photometric map is valid only for the motion and exposure used to
        // measure it. Missing or stale evidence cannot authorize a match.
        if(!HasCurrentCaptureReliability(source)||motion.alignmentConfidence<=0)return result;
        const auto& map=*source.reliability;
        double reliability=0;std::uint16_t bits=0;
        if(!Mfd::ReadReliabilityStoreRawCoordinate(map,point,reliability,&bits,nullptr)||
           HardCaptureRejection(bits)||!std::isfinite(reliability))return result;
        // Sparse texture is uncertainty, not contradictory evidence. Keep the
        // conservative floor while retaining the measured positional risk.
        result.confidence=std::min(std::clamp(std::max(reliability,
            kUncertainCaptureReliability),0.,1.),refinementConfidence);
    } else result=global(source.alignmentDiagnostic.localApplied?0:1);
    if(result.confidence>0&&source.depthSupport&&source.depthSupport->width&&source.depthSupport->height&&
       source.frame.activeExtent.width&&source.frame.activeExtent.height) {
        const auto& depth=*source.depthSupport;
        const auto x=unsigned(std::clamp(point.x*depth.width/source.frame.activeExtent.width,0.,double(depth.width-1)));
        const auto y=unsigned(std::clamp(point.y*depth.height/source.frame.activeExtent.height,0.,double(depth.height-1)));
        const auto p=std::size_t(y)*depth.width+x;
        if(p<depth.rejected.size()&&depth.rejected[p])result.confidence=0;
    }
    return result;
}
}
