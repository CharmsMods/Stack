#include "CaptureCorrespondence.h"
#include "ProcessingInternal.h"
#include <algorithm>
#include <cmath>

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
CaptureCorrespondence EvaluateCaptureCorrespondence(const PreparedSource& source,
    Mfd::RawCoordinate point,bool reference,Mfd::LocalMotionFieldSample* evaluatedMotion) {
    if(reference)return {point,{},1};
    const double floor=source.alignmentDiagnostic.model==CaptureAlignment::Model::Identity?0:.0025;
    const auto global=[&](double confidence) {
        return CaptureCorrespondence{source.alignment.Map(point),{floor,0,floor},confidence};
    };
    if(!std::isfinite(point.x)||!std::isfinite(point.y))return global(0);
    CaptureCorrespondence result;
    if(source.localMotion) {
        Mfd::LocalMotionFieldSample motion;double refinementConfidence=1;
        if(!EvaluateCaptureMotion(source,point,{},motion,&refinementConfidence)) {
            return global(0);
        }
        if(evaluatedMotion)*evaluatedMotion=motion;
        result.sourceRaw=motion.sourceRaw;result.covarianceRaw=motion.covarianceRaw;
        result.confidence=0;
        // A photometric map is valid only for the motion and exposure used to
        // measure it. Missing or stale evidence cannot authorize a match.
        if(!HasCurrentCaptureReliability(source)||motion.alignmentConfidence<=0)return result;
        const auto& map=*source.reliability;
        if(!map.cellExtent.width||!map.cellExtent.height||!source.frame.activeExtent.width||
           !source.frame.activeExtent.height)return result;
        const auto cell=[](double p,std::uint64_t cells,std::uint64_t pixels) {
            return static_cast<std::uint32_t>(std::clamp(p*cells/pixels,0.,double(cells-1)));
        };
        double reliability=0;std::uint16_t bits=0;
        if(!Mfd::ReadReliabilityStoreCell(map,
            cell(point.x,map.cellExtent.width,source.frame.activeExtent.width),
            cell(point.y,map.cellExtent.height,source.frame.activeExtent.height),
            reliability,&bits,nullptr)||HardCaptureRejection(bits)||!std::isfinite(reliability))return result;
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
