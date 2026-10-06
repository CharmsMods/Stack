#pragma once
#include "Raw/Bracketing/BurstGuide.h"
#include "Raw/Bracketing/BurstAlignment.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingBurstAlignment() {
    using namespace Raw::Bracketing;using namespace Raw::Mfd;
    const auto check=[](bool value,const std::string& message){if(!value)throw std::runtime_error(message);};
    std::array<double,25> a{},b{},variance{};variance.fill(1e-6);
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)a[y*5+x]=b[y*5+x]=.1+.003*x+.002*y;
    RawSignalGradient gradient;double disagreement=0;
    check(RepeatedBurstStructure(a,b,variance,variance,gradient,disagreement),"Independent burst subsets lost repeatable structure");
    b.fill(.1);check(!RepeatedBurstStructure(a,b,variance,variance,gradient,disagreement),"One noisy subset nominated its own detail");
    for(unsigned i=0;i<25;++i)b[i]=.2-a[i];
    check(!RepeatedBurstStructure(a,b,variance,variance,gradient,disagreement),"Opposing burst structure passed agreement");
    a.fill(.1);b.fill(.1);
    check(!RepeatedBurstStructure(a,b,variance,variance,gradient,disagreement),"Flat burst requested additional alignment");

    CfaLayout layout;check(CfaLayout::TryCreate(Raw::CfaPattern::RGGB,layout),"Burst fixture CFA");
    RegistrationParameters parameters;parameters.pyramidLevels=2;parameters.finestPatchPlanePixels=16;
    const auto pyramid=[&](double dx,double dy,bool colorConflict) {
        std::array<CfaPyramidBasePlane,4> base;
        for(unsigned c=0;c<4;++c) {
            auto& plane=base[c];plane.site=static_cast<CfaSite>(c);plane.extent={64,64};
            plane.signal.resize(64*64);plane.variance.assign(64*64,1e-6);plane.validMask.assign(64*64,1);
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const double xx=x-dx*(colorConflict&&c==3?-2:1),yy=y-dy;
                plane.signal[y*64+x]=.3+.12*std::sin(xx*.21+yy*.13)+.08*std::cos(xx*.07-yy*.17)+c*.01;
            }
        }
        CfaPlanePyramid result;std::string error;
        check(BuildCfaPlanePyramid(layout,{128,128},base,parameters,result,&error),error);return result;
    };
    auto reference=pyramid(0,0,false),alternate=pyramid(.35,-.2,false);
    BidirectionalLocalMotionRequest request;request.reference=&reference;request.alternate=&alternate;
    request.options.registration=parameters;MotionNode node;
    check(RefineBidirectionalMotionPoint(request,{64,64},{64,64},node),"Supported subpixel correction was not accepted");
    check(std::hypot(node.residualRaw.x-.7,node.residualRaw.y+.4)<.12,"Subpixel correction moved away from the known displacement");
    check(node.forwardBackwardEuclideanRaw<.15,"Refined motion lost forward/reverse closure");
    alternate=reference;
    check(!RefineBidirectionalMotionPoint(request,{64,64},{64,64},node),"Exact correspondence was unnecessarily refined");
    alternate=pyramid(.35,-.2,true);
    check(!RefineBidirectionalMotionPoint(request,{64,64},{64,64},node),"Contradictory single-color motion was accepted");
    request.shouldCancel=[] {return true;};
    check(!RefineBidirectionalMotionPoint(request,{64,64},{64,64},node),"Canceled point refinement continued");
    PreparedSource source;source.frame.activeExtent={128,128};
    source.localMotion=std::make_shared<LocalMotionGrid>();auto& grid=*source.localMotion;
    grid.valid=true;grid.width=grid.height=3;grid.spacingRawX=grid.spacingRawY=64;
    grid.referenceRawExtent=grid.sourceRawExtent={128,128};grid.nodes.resize(9);
    for(auto& cell:grid.nodes){cell.state=MotionNodeState::Structured;cell.confidence=1;cell.covarianceRaw={.0025,0,.0025};}
    LocalMotionFieldSample unchanged;check(EvaluateCaptureMotion(source,{64,64},{},unchanged),"Base motion fixture invalid");
    auto field=std::make_shared<MotionRefinementField>();field->width=field->height=5;field->spacing=32;field->cells.resize(25);
    field->cells[12].delta={.4,-.2};field->cells[12].confidence=.8f;field->cells[12].covariance={.0025,0,.0025};source.refinedMotion=field;
    double correctionConfidence=0;
    LocalMotionFieldSample corrected;check(EvaluateCaptureMotion(source,{64,64},{},corrected,&correctionConfidence),"Valid refinement overlay failed");
    check(std::abs(corrected.sourceRaw.x-64.4)<1e-12&&corrected.covarianceRaw.xxRawPixelsSquared>=unchanged.covarianceRaw.xxRawPixelsSquared,
        "Refinement lost displacement or understated alignment uncertainty");
    check(std::abs(correctionConfidence-.8)<1e-6,"Refinement confidence was not exposed to the sample reader");
    check(EvaluateCaptureMotion(source,{32,32},{},corrected,&correctionConfidence)&&correctionConfidence==1,
        "An unsupported refinement changed the original confidence policy");
    for(auto& cell:grid.nodes){cell.state=MotionNodeState::Rejected;cell.confidence=0;}
    check(!EvaluateCaptureMotion(source,{64,64},{},corrected),"Refinement resurrected rejected base motion");
    std::cout<<"Burst alignment: independent subsets, flat fallback, subpixel improvement, color conflict and cancellation passed.\n";
}
}
