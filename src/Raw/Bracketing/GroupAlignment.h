#pragma once
#include "ProcessingInternal.h"
#include "CaptureMotion.h"
#include "BorderSupport.h"
#include "Raw/MultiFrameDenoise/MotionCovariance.h"
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
// Both links must have accepted geometry; uncertainty propagates through all
// supported one-sided Jacobians of the second link, including grid knots.
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
    const double maximumSigma=options.registration.warpCovarianceEigenvalueMaxRawPixels;
    const double maximumVariance=maximumSigma*maximumSigma;
    const auto covarianceSupported=[&](const Mfd::SymmetricRawCovariance& covariance) {
        double low=0,high=0;
        return std::isfinite(maximumVariance)&&maximumVariance>0&&
            Mfd::TryMotionCovarianceEigenvalues(covariance,low,high)&&high<=maximumVariance;
    };
    for(auto& node:out.nodes) {
        const auto point=node.centerRaw;const auto gx=node.gridX,gy=node.gridY;
        node={};node.centerRaw=point;node.gridX=gx;node.gridY=gy;
        Mfd::LocalMotionFieldSample a,b;
        Mfd::RawMappingJacobianSet linearizations;
        if(!EvaluateCaptureMotion(anchor,point,options,a)||a.alignmentConfidence<=0||
           !Mfd::EvaluateLocalMotionField(pair,a.sourceRaw,options,b,nullptr,false,nullptr,&linearizations)||
           b.alignmentConfidence<=0) {
            node.rejectReason=Mfd::MotionNodeRejectReason::MotionFieldAmbiguous;++out.rejectedCount;continue;
        }
        Mfd::SymmetricRawCovariance transported;
        if(!Mfd::TryTransportMotionCovarianceEnvelope(a.covarianceRaw,linearizations,transported)) {
            node.rejectReason=Mfd::MotionNodeRejectReason::InvalidCovariance;++out.rejectedCount;continue;
        }
        node.covarianceRaw={
            b.covarianceRaw.xxRawPixelsSquared+transported.xxRawPixelsSquared,
            b.covarianceRaw.xyRawPixelsSquared+transported.xyRawPixelsSquared,
            b.covarianceRaw.yyRawPixelsSquared+transported.yyRawPixelsSquared};
        // A later field sample retains the configured covariance ceiling.
        // Do not publish composed evidence that it would make more certain
        // by clipping this known local-model envelope down to that ceiling.
        if(!covarianceSupported(node.covarianceRaw)) {
            node.rejectReason=Mfd::MotionNodeRejectReason::InvalidCovariance;++out.rejectedCount;continue;
        }
        const auto base=source.alignment.Map(point);
        node.residualRaw={b.sourceRaw.x-base.x,b.sourceRaw.y-base.y};node.discreteResidualRaw=node.residualRaw;
        node.confidence=std::min(a.alignmentConfidence,b.alignmentConfidence);
        node.validFraction=1;node.state=Mfd::MotionNodeState::Structured;
        node.positionalSigmaRaw=std::sqrt(std::max(node.covarianceRaw.xxRawPixelsSquared,node.covarianceRaw.yyRawPixelsSquared));
        ++out.structuredCount;
    }
    ExtendMotionBoundary(out);
    // Boundary extension adds uncertainty after interior validation. Reject
    // oversized copied nodes too; otherwise their extra variance would be
    // clipped away on evaluation. Generic cell-interpolation policy is unchanged.
    out.structuredCount=out.flatSafeCount=out.rejectedCount=0;
    for(auto& node:out.nodes) {
        if(node.state!=Mfd::MotionNodeState::Rejected&&!covarianceSupported(node.covarianceRaw)) {
            node.state=Mfd::MotionNodeState::Rejected;
            node.rejectReason=Mfd::MotionNodeRejectReason::InvalidCovariance;
            node.confidence=0;
        }
        if(node.state==Mfd::MotionNodeState::Structured)++out.structuredCount;
        else if(node.state==Mfd::MotionNodeState::FlatSafe)++out.flatSafeCount;
        else ++out.rejectedCount;
    }
    // Preserve the validated field descriptor even when every node rejects:
    // bracket reliability consumes that field as zero local evidence. An
    // invalid descriptor would instead abort reliability for the whole bracket.
    return out;
}
}
