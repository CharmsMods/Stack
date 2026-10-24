#pragma once
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/CaptureCorrespondence.h"
#include "Raw/Bracketing/GuideCovariance.h"
#include "Raw/Bracketing/GroupAlignment.h"
#include <limits>
#include <cstring>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingGuideCoordinates() {
    using namespace Raw::Mfd;
    using namespace Raw::Bracketing;
    const char* phase="transport";
    const auto require=[&](bool ok){if(!ok)throw std::runtime_error(std::string("Bracketing guide-coordinate invariant failed: ")+phase);};
    const auto sameBits=[](double a,double b){return std::memcmp(&a,&b,sizeof(double))==0;};
    const auto near=[&](double a,double b){require(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<1e-8);};
    const SymmetricRawCovariance covariance{4,.3,1};
    const RawSignalGradient guide{1,.2};
    for(const auto linear:{std::array<double,4>{1,0,0,1},{0,-1,1,0},{1,.1,0,1},{1.1,0,0,.9}}) {
        RawMappingJacobian j{linear,true};SymmetricRawCovariance transformed;
        require(TryGuideCovariance(covariance,j,transformed));
        const double det=linear[0]*linear[3]-linear[1]*linear[2];
        const RawSignalGradient sourceGradient{(linear[3]*guide.dxPerRawPixel-linear[2]*guide.dyPerRawPixel)/det,
            (-linear[1]*guide.dxPerRawPixel+linear[0]*guide.dyPerRawPixel)/det};
        double expected=0,actual=0;
        require(RegistrationUncertaintyVariance(sourceGradient,covariance,expected));
        require(RegistrationUncertaintyVariance(guide,transformed,actual));near(actual,expected);
    }
    for(auto j:{RawMappingJacobian{{1,0,0,0},true},RawMappingJacobian{{1,0,0,1},false},
        RawMappingJacobian{{std::numeric_limits<double>::infinity(),0,0,1},true}}) {
        SymmetricRawCovariance unchanged{7,0,7};require(!TryGuideCovariance(covariance,j,unchanged));near(unchanged.xxRawPixelsSquared,7);
    }
    { SymmetricRawCovariance result;
      require(TryGuideCovariance({-1e-14,0,1},{{1,0,0,1},true},result));require(result.xxRawPixelsSquared==0);
      require(!TryGuideCovariance({-1,0,1},{{1,0,0,1},true},result));
      require(!TryGuideCovariance({1,2,1},{{1,0,0,1},true},result)); }
    PreparedSource source;source.frame.activeExtent={64,64};
    source.alignment.linear={1,.1,0,1};RawMappingJacobian global;
    EvaluateCaptureCorrespondence(source,{16,16},false,nullptr,&global);
    require(global.valid&&global.linear==source.alignment.linear);
    source.localMotion=std::make_shared<LocalMotionGrid>();auto& grid=*source.localMotion;
    grid.valid=true;grid.width=grid.height=3;grid.spacingRawX=grid.spacingRawY=24;
    grid.referenceRawExtent=grid.sourceRawExtent={64,64};grid.nodes.resize(9);
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){auto& n=grid.nodes[y*3+x];
        n.state=MotionNodeState::Structured;n.confidence=1;n.centerRaw={24.*x,24.*y};
        n.residualRaw={.01*24*x,.02*24*y};n.covarianceRaw={.01,0,.01};}
    const auto evaluate=[&](RawCoordinate point,RawMappingJacobian* j=nullptr){
        LocalMotionFieldSample value;require(EvaluateCaptureMotion(source,point,{},value,nullptr,j));return value;};
    const auto checkDerivative=[&](RawCoordinate p){RawMappingJacobian j;const auto with=evaluate(p,&j),without=evaluate(p);
        require(j.valid);
        require(with.valid==without.valid&&with.rejectReason==without.rejectReason&&with.message==without.message&&
            sameBits(with.sourceRaw.x,without.sourceRaw.x)&&sameBits(with.sourceRaw.y,without.sourceRaw.y)&&
            sameBits(with.residualRaw.x,without.residualRaw.x)&&sameBits(with.residualRaw.y,without.residualRaw.y)&&
            sameBits(with.alignmentConfidence,without.alignmentConfidence)&&sameBits(with.gridConfidence,without.gridConfidence)&&
            sameBits(with.disagreementSigmaRaw,without.disagreementSigmaRaw)&&
            sameBits(with.covarianceRaw.xxRawPixelsSquared,without.covarianceRaw.xxRawPixelsSquared)&&
            sameBits(with.covarianceRaw.xyRawPixelsSquared,without.covarianceRaw.xyRawPixelsSquared)&&
            sameBits(with.covarianceRaw.yyRawPixelsSquared,without.covarianceRaw.yyRawPixelsSquared));
        constexpr double h=1e-4;auto a=evaluate({p.x-h,p.y}),b=evaluate({p.x+h,p.y});
        near((b.sourceRaw.x-a.sourceRaw.x)/(2*h),j.linear[0]);near((b.sourceRaw.y-a.sourceRaw.y)/(2*h),j.linear[2]);
        a=evaluate({p.x,p.y-h});b=evaluate({p.x,p.y+h});
        near((b.sourceRaw.x-a.sourceRaw.x)/(2*h),j.linear[1]);near((b.sourceRaw.y-a.sourceRaw.y)/(2*h),j.linear[3]);};
    phase="local linear";checkDerivative({12,12});checkDerivative({24,12});
    RawMappingJacobian edge;evaluate({0,12},&edge);require(edge.valid);near(edge.linear[0],1.01);
    {
        phase="composed field";PreparedSource anchor,composed;anchor.frame.activeExtent=composed.frame.activeExtent={64,64};
        anchor.localMotion=std::make_shared<LocalMotionGrid>(grid);
        for(auto& n:anchor.localMotion->nodes)n.residualRaw={};
        anchor.localMotion->globalWarp.linear={1,.05,0,1};
        LocalMotionGrid pair=*anchor.localMotion;pair.globalWarp.linear={1,0,.03,1};
        ExtendMotionBoundary(*anchor.localMotion); // Composition consumes the production extended anchor.
        composed.alignment.linear={1,.05,.03,1.0015};
        composed.localMotion=std::make_shared<LocalMotionGrid>(ComposeGroupMotion(anchor,composed,pair,{}));
        LocalMotionFieldSample sample;RawMappingJacobian j;
        require(EvaluateCaptureMotion(composed,{12,12},{},sample,nullptr,&j)&&j.valid);
        for(unsigned k=0;k<4;++k)near(j.linear[k],composed.alignment.linear[k]);
    }
    grid.nodes[0].confidence=.3;grid.nodes[1].confidence=.8;grid.nodes[3].state=MotionNodeState::Rejected;
    phase="sparse weighted";checkDerivative({12,12});
    for(auto& n:grid.nodes){n.confidence=1;n.state=MotionNodeState::Structured;}
    phase="local seam";grid.nodes[2].residualRaw.x+=.4;RawMappingJacobian seam;evaluate({24,12},&seam);require(!seam.valid);
    grid.nodes[2].residualRaw.x-=.4;
    auto refinement=std::make_shared<MotionRefinementField>();source.refinedMotion=refinement;auto& field=*refinement;
    field.width=field.height=3;field.spacing=16;field.cells.resize(9);
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){auto& c=field.cells[y*3+x];c.confidence=1;c.delta={.03*16*y,-.01*16*x};}
    phase="refined linear";checkDerivative({12,12});checkDerivative({16,12});
    RawMappingJacobian refined;evaluate({12,12},&refined);near(refined.linear[0],1.01);near(refined.linear[1],.03);
    near(refined.linear[2],-.01);near(refined.linear[3],1.02);
    phase="refinement domain";evaluate({32,12},&seam);require(!seam.valid); // Refinement ends inside sensor with nonzero correction.
    checkDerivative({36,12}); // Outside refinement domain: exact base derivative.
    phase="refinement seam";field.cells[2].delta.x+=.5;evaluate({16,12},&seam);require(!seam.valid);field.cells[2].delta.x-=.5;
    for(auto& c:field.cells){c.delta={100,0};c.confidence=1;}
    phase="outside source";checkDerivative({12,12}); // Out-of-source correction is not applied.
    // Binary-exact switch location; a nearly-boundary point has a valid
    // one-sided branch derivative and must not be mistaken for the switch.
    for(auto& n:grid.nodes)n.residualRaw.x=0;
    for(auto& c:field.cells)c.delta={52,0};
    phase="source switch";evaluate({12,12},&seam);require(!seam.valid); // Discontinuous correction switch.
    for(auto& c:field.cells){c.delta={};c.confidence=0;}
    phase="empty correction";checkDerivative({12,12}); // Empty correction does not remove a valid derivative.
    // The selected right-hand cell has no active corrections, but a left
    // neighbor approaches zero with a nonzero derivative at the seam.
    field.cells[0].confidence=field.cells[3].confidence=1;
    field.cells[0].delta=field.cells[3].delta={.2,0};
    phase="zero-mass seam";evaluate({16,12},&seam);require(!seam.valid);

}
} // namespace Stack::Validation
