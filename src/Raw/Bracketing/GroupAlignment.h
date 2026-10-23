#pragma once
#include "ProcessingInternal.h"
#include "CaptureMotion.h"
#include "BorderSupport.h"
#include <cmath>

namespace Raw::Bracketing {
inline bool GroupRelativeWarp(const PreparedSource& anchor,const PreparedSource& source,Mfd::AffineModel& pair) {
    Mfd::AffineModel inverse;
    if(!Mfd::InvertAffineModel(anchor.alignment,inverse))return false;
    pair={};
    const auto zero=source.alignment.Map(inverse.Map({0,0}));
    const auto x=source.alignment.Map(inverse.Map({1,0}));
    const auto y=source.alignment.Map(inverse.Map({0,1}));
    pair.translationRaw=zero;pair.linear={x.x-zero.x,y.x-zero.x,x.y-zero.y,y.y-zero.y};
    return true;
}

// Compose origin -> exposure anchor -> capture ONCE at motion nodes. The
// full-resolution samplers retain a single field and their existing cost.
// Both links must have accepted geometry; uncertainty propagates through the
// second link's measured Jacobian rather than treating the anchor as exact.
inline Mfd::LocalMotionGrid ComposeGroupMotion(const PreparedSource& anchor,
    const PreparedSource& source,const Mfd::LocalMotionGrid& pair,const Mfd::LocalMotionOptions& options) {
    auto out=*anchor.localMotion;out.globalWarp=source.alignment;
    // AlignLocal extends every anchor by one node ring. Compose its interior
    // first: evaluating the extrapolated ring as real sensor coordinates would
    // reject it and unnecessarily remove support along the output boundary.
    if(out.width>2&&out.height>2) {
        const auto previous=std::move(out.nodes);const auto previousWidth=out.width;
        out.width-=2;out.height-=2;
        out.originRawX+=out.spacingRawX;out.originRawY+=out.spacingRawY;
        out.nodes.resize(std::size_t(out.width)*out.height);
        for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x) {
            auto& node=out.nodes[std::size_t(y)*out.width+x];
            node=previous[std::size_t(y+1)*previousWidth+x+1];node.gridX=x;node.gridY=y;
        }
    }
    out.sourceRawExtent=source.frame.activeExtent;
    out.structuredCount=out.flatSafeCount=out.rejectedCount=0;
    out.message="Within-exposure motion composed with the exposure anchor.";
    for(auto& node:out.nodes) {
        const auto point=node.centerRaw;const auto gx=node.gridX,gy=node.gridY;
        node={};node.centerRaw=point;node.gridX=gx;node.gridY=gy;
        Mfd::LocalMotionFieldSample a,b,x0,x1,y0,y1;
        const auto sample=[&](Mfd::RawCoordinate p,Mfd::LocalMotionFieldSample& v) {
            return Mfd::EvaluateLocalMotionField(pair,p,options,v,nullptr,false)&&v.alignmentConfidence>0;
        };
        if(!EvaluateCaptureMotion(anchor,point,options,a)||a.alignmentConfidence<=0||!sample(a.sourceRaw,b)) {
            node.rejectReason=Mfd::MotionNodeRejectReason::MotionFieldAmbiguous;++out.rejectedCount;continue;
        }
        const double left=std::max(0.,a.sourceRaw.x-.5),right=std::min(double(anchor.frame.activeExtent.width-1),a.sourceRaw.x+.5);
        const double top=std::max(0.,a.sourceRaw.y-.5),bottom=std::min(double(anchor.frame.activeExtent.height-1),a.sourceRaw.y+.5);
        if(right<=left||bottom<=top||!sample({left,a.sourceRaw.y},x0)||!sample({right,a.sourceRaw.y},x1)||
            !sample({a.sourceRaw.x,top},y0)||!sample({a.sourceRaw.x,bottom},y1)) {
            node.rejectReason=Mfd::MotionNodeRejectReason::MotionFieldAmbiguous;++out.rejectedCount;continue;
        }
        const double j00=(x1.sourceRaw.x-x0.sourceRaw.x)/(right-left),j01=(y1.sourceRaw.x-y0.sourceRaw.x)/(bottom-top);
        const double j10=(x1.sourceRaw.y-x0.sourceRaw.y)/(right-left),j11=(y1.sourceRaw.y-y0.sourceRaw.y)/(bottom-top);
        const auto& c=a.covarianceRaw;
        node.covarianceRaw={
            b.covarianceRaw.xxRawPixelsSquared+j00*j00*c.xxRawPixelsSquared+2*j00*j01*c.xyRawPixelsSquared+j01*j01*c.yyRawPixelsSquared,
            b.covarianceRaw.xyRawPixelsSquared+j00*j10*c.xxRawPixelsSquared+(j00*j11+j01*j10)*c.xyRawPixelsSquared+j01*j11*c.yyRawPixelsSquared,
            b.covarianceRaw.yyRawPixelsSquared+j10*j10*c.xxRawPixelsSquared+2*j10*j11*c.xyRawPixelsSquared+j11*j11*c.yyRawPixelsSquared};
        const auto base=source.alignment.Map(point);
        node.residualRaw={b.sourceRaw.x-base.x,b.sourceRaw.y-base.y};node.discreteResidualRaw=node.residualRaw;
        node.confidence=std::min(a.alignmentConfidence,b.alignmentConfidence);
        node.validFraction=1;node.state=Mfd::MotionNodeState::Structured;
        node.positionalSigmaRaw=std::sqrt(std::max(node.covarianceRaw.xxRawPixelsSquared,node.covarianceRaw.yyRawPixelsSquared));
        ++out.structuredCount;
    }
    ExtendMotionBoundary(out);
    return out;
}
}
