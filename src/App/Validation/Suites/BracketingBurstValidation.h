#pragma once
#include "Raw/Bracketing/BurstGuide.h"
#include "Raw/Bracketing/BurstAlignment.h"
#include "Raw/Bracketing/CaptureCorrespondence.h"
#include "Raw/Bracketing/CaptureReliability.h"
#include "Raw/Bracketing/PhotometricReliability.h"
#include "Raw/Bracketing/GroupMemory.h"
#include "Raw/Bracketing/SuperResolution/Preparation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingReliabilityCoordinates() {
    using namespace Raw::Bracketing;using namespace Raw::Mfd;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    // The normal large-image profile has four RAW pixels per evidence cell.
    // These are actual correspondence/store calls, not an image generator.
    for(const auto extent:{PixelExtent{4050,1000},PixelExtent{1000,4050},PixelExtent{4048,1000}}) {
        PreparedSource source;source.frame.activeExtent=extent;
        source.localMotion=std::make_shared<LocalMotionGrid>();auto& grid=*source.localMotion;
        grid.valid=true;grid.width=grid.height=2;grid.spacingRawX=double(extent.width-1);grid.spacingRawY=double(extent.height-1);
        grid.referenceRawExtent=grid.sourceRawExtent=extent;grid.nodes.resize(4);
        for(auto& node:grid.nodes){node.state=MotionNodeState::Structured;node.confidence=1;node.covarianceRaw={.0025,0,.0025};}
        ReliabilityMap map;map.valid=true;map.rawExtent=extent;map.cellStrideBayerCells=2;
        map.cellExtent={(extent.width/2+1)/2,(extent.height/2+1)/2};
        map.cells.resize(std::size_t(map.cellExtent.width)*map.cellExtent.height);
        const bool vertical=extent.height>extent.width;
        for(unsigned y=0;y<map.cellExtent.height;++y)for(unsigned x=0;x<map.cellExtent.width;++x) {
            auto& cell=map.cells[std::size_t(y)*map.cellExtent.width+x];cell.reliability=1;
            if((vertical?y:x)==506)cell.rejectionBits=ReliabilityRejectMask(ReliabilityRejectBit::PatchRejected);
        }
        source.reliability=std::make_shared<ReliabilityStore>();std::string error;
        check(BuildReliabilityStore(map,ReliabilityStorageFormat::Uint16Unorm,32,*source.reliability,&error),
            "Reliability coordinate store could not be built");
        StampCaptureReliability(source);
        for(double coordinate:{2023.999,2024.,2026.,2027.,2027.999,2028.}) {
            const RawCoordinate point=vertical?RawCoordinate{500,coordinate}:RawCoordinate{coordinate,500};
            const bool rejected=coordinate>=2024&&coordinate<2028;
            check((EvaluateCaptureCorrespondence(source,point).confidence==0)==rejected,
                "Reference-grid hard rejection moved at a partial final evidence block");
        }
        const auto last=vertical?RawCoordinate{500,double(extent.height-1)}:RawCoordinate{double(extent.width-1),500};
        check(EvaluateCaptureCorrespondence(source,last).confidence>0,
            "Short final reliability block lost its last RAW pixel");
        if(extent.width==4050) {
            // The stored evidence is indexed in reference coordinates, not by
            // a source raster's dimensions. No source-size normalization belongs here.
            source.frame.activeExtent.width=5000;grid.sourceRawExtent.width=5000;
            check(EvaluateCaptureCorrespondence(source,{2027,500}).confidence==0&&
                EvaluateCaptureCorrespondence(source,{2028,500}).confidence>0,
                "Reliability lookup used source extent instead of stored reference geometry");
        }
    }
    LocalMotionGrid motion;motion.valid=true;motion.width=motion.height=2;
    motion.referenceRawExtent=motion.sourceRawExtent={10,14};motion.spacingRawX=9;motion.spacingRawY=13;
    motion.nodes.resize(4);for(auto& node:motion.nodes){node.state=MotionNodeState::Structured;node.confidence=1;node.covarianceRaw={.0025,0,.0025};}
    ReliabilityBuildRequest request;request.rawExtent={10,14};request.cellStrideBayerCells=2;
    request.motionGrid=&motion;CfaLayout::TryCreate(Raw::CfaPattern::RGGB,request.layout);
    request.residualEvaluator=[](RawCoordinate,CfaSite,const LocalMotionFieldSample&,ReliabilityResidualSample& sample) {
        sample.valid=sample.hardValid=true;sample.referenceValue=sample.alternateValue=.2;
        sample.referenceGateVariance=sample.alternateGateVariance=.0001;return true;
    };
    ReliabilityMap built;std::string error;
    check(BuildColorReliability(request,built,error)&&built.cellStrideBayerCells==2&&
        built.rawExtent.width==10&&built.rawExtent.height==14&&built.cellExtent.width==3&&built.cellExtent.height==4,
        "Burst color-reliability builder lost its exact reference grid");

}

inline void ValidateBracketingLocalAlignmentMemory() {
    using namespace Raw::Bracketing;using namespace Raw::Mfd;
    const auto check=[](bool value,const std::string& message){if(!value)throw std::runtime_error(message);};
    constexpr std::uint64_t mib=1024ull*1024;
    ProcessingRequest request;request.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
    request.memoryBudgetBytes=512*mib;request.workerCount=1;
    PreparedDataset data;data.width=3648;data.height=2736;data.sources.resize(2);
    const auto shape=[&](unsigned width,unsigned height) {
        data.width=width;data.height=height;
        for(auto& source:data.sources)source.frame.activeExtent={width,height};
    };
    shape(3648,2736);for(auto& source:data.sources)source.frame.tileRawPixels=128;
    const std::uint64_t pixels=std::uint64_t(data.width)*data.height;
    // Normal 10MP pair: the older decoder/group gates admit this shape, but
    // pair pyramids + reference inspection + color evidence alone exceed512MiB.
    check(pixels<=request.memoryBudgetBytes/48&&GroupWorkerFits(request.memoryBudgetBytes,
        GroupOutputReserveBytes(pixels),GroupWorkerBytes(1,128,2,request.memoryBudgetBytes)),
        "Local memory regression no longer reaches the prior admission gap");
    std::uint64_t required=0,colorBytes=0;std::string error;
    const auto cells=std::uint64_t(data.width/4)*(data.height/4);
    check(ColorReliabilityWorkingBytes(cells,colorBytes),"Color evidence sizing failed");
    const auto pairBytes=2*(17*(pixels+pixels/4+pixels/16+pixels/64)+pixels);
    check(pairBytes+pixels/4*6+colorBytes>request.memoryBudgetBytes,
        "Concrete simultaneous host payload no longer exceeds the old allowance");
    check(!AdmitLocalAlignmentMemory(request,data,required,error)&&required>request.memoryBudgetBytes&&
        error.find("memory budget")!=std::string::npos,"Two-capture local host budget was bypassed");
    const auto expected=72*pixels+6*(pixels/4)+10*cells+64*mib;
    check(required==expected,"Local stage maximum changed unexpectedly");
    std::vector<std::string> diagnostics;
    check(!PreflightBurstRefinement(request,data,diagnostics,error),
        "Early preflight still bypasses two-capture local memory");
    request.memoryBudgetBytes=1024*mib;
    check(AdmitLocalAlignmentMemory(request,data,required,error)&&required==expected&&
        PreflightBurstRefinement(request,data,diagnostics,error),"Sufficient local host memory was rejected");
    request.memoryBudgetBytes=expected;
    check(AdmitLocalAlignmentMemory(request,data,required,error),"Exact local allowance was rejected");
    --request.memoryBudgetBytes;
    check(!AdmitLocalAlignmentMemory(request,data,required,error),"One byte below local allowance was accepted");
    request.memoryBudgetBytes=512*mib;data.sources[0].enabled=false;
    check(!AdmitLocalAlignmentMemory(request,data,required,error),
        "Disabled fixed reference hid an enabled alternate's pair memory");
    data.sources[1].enabled=false;request.memoryBudgetBytes=1;
    check(AdmitLocalAlignmentMemory(request,data,required,error)&&required==0,
        "No-alternate request acquired an unused pair requirement");
    data.sources[1].enabled=true;request.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
    check(AdmitLocalAlignmentMemory(request,data,required,error)&&required==0,
        "Nonlocal alignment acquired an unused pair requirement");
    request.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
    request.memoryBudgetBytes=std::numeric_limits<std::uint64_t>::max();
    for(const auto width:{0u,1u,3u}) {
        data.width=width;
        check(!AdmitLocalAlignmentMemory(request,data,required,error),"Invalid local Bayer width was admitted");
    }
    shape(std::numeric_limits<unsigned>::max()-1,std::numeric_limits<unsigned>::max()-1);
    check(!AdmitLocalAlignmentMemory(request,data,required,error)&&required==0&&error.find("overflow")!=std::string::npos,
        "Overflowed local plan was admitted at UINT64_MAX budget");
    check(!ColorReliabilityWorkingBytes(std::numeric_limits<std::uint64_t>::max(),colorBytes)&&colorBytes==0,
        "Overflowed color-evidence payload was admitted");
    shape(2,1000);
    // Narrow/partial levels need ceil division, not a geometric-series shortcut.
    const auto narrowPair=2*(17ull*(2000+1000+500+252)+2000);
    check(AdmitLocalAlignmentMemory(request,data,required,error),error);
    check(ColorReliabilityWorkingBytes(500,colorBytes),"Narrow color evidence sizing failed");
    const auto narrowWorking=std::max({72ull*2000,narrowPair+colorBytes,
        narrowPair+2000+512ull*1024,narrowPair/2+9ull*2*128*9});
    check(required==6*500+10*500+narrowWorking+64*mib,"Partial CFA pyramid accounting lost ceil-rounded levels");
    check(CaptureReliabilityStrideBayerCells({1998,1000})==1&&
        CaptureReliabilityStrideBayerCells({2000,1000})==2&&
        CaptureReliabilityStrideBayerCells({std::numeric_limits<std::uint64_t>::max(),2})==2,
        "Reliability memory/profile stride disagrees at its area boundary");
    shape(3648,2736);data.sources[1].frame.activeExtent.width+=2;
    check(!AdmitLocalAlignmentMemory(request,data,required,error),"Mismatched prepared extent was admitted");
    shape(3648,2736);data.sources[0].frame.tileRawPixels=0;
    check(!AdmitLocalAlignmentMemory(request,data,required,error),"Invalid reference tile size was admitted");
    std::cout<<"Local alignment host admission: two-capture gap, retained reference, stage maxima and overflow passed.\n";
}

inline void ValidateBracketingBurstAlignment() {
    ValidateBracketingReliabilityCoordinates();
    ValidateBracketingLocalAlignmentMemory();
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
    ReliabilityMap reliability;reliability.valid=true;reliability.rawExtent={128,128};
    reliability.cellExtent={64,64};reliability.cellStrideBayerCells=1;reliability.cells.resize(64*64);
    for(auto& cell:reliability.cells)cell.reliability=1;
    const auto install=[&](std::uint16_t bits,double confidence) {
        for(auto& cell:reliability.cells){cell.rejectionBits=bits;cell.reliability=confidence;}
        source.reliability=std::make_shared<ReliabilityStore>();std::string error;
        check(BuildReliabilityStore(reliability,ReliabilityStorageFormat::Uint16Unorm,32,*source.reliability,&error),error);
        StampCaptureReliability(source);
    };
    const auto consistent=[&]() {
        const auto match=EvaluateCaptureCorrespondence(source,{64,64});
        const auto sr=Sr::MapCapture(source,64,64,false);
        check(std::abs(sr.x-match.sourceRaw.x)<1e-5&&std::abs(sr.y-match.sourceRaw.y)<1e-5&&
            std::abs(sr.z-match.confidence)<1e-6&&std::abs(sr.w-(match.covarianceRaw.xxRawPixelsSquared+
            match.covarianceRaw.yyRawPixelsSquared))<1e-6,"Standard/SR correspondence or uncertainty differs");
        return match.confidence;
    };
    install(0,1);check(std::abs(consistent()-.8)<1e-6,"Stored reliability erased correction confidence");
    auto depthSupport=std::make_shared<Raw::DepthCorrespondenceSupport>();
    depthSupport->width=depthSupport->height=2;depthSupport->rejected={0,0,0,1};
    source.depthSupport=depthSupport;
    check(consistent()==0,"Standard/SR ignored geometric disagreement");
    depthSupport->rejected[3]=0;
    check(consistent()>0,"Unknown depth removed accepted capture evidence");
    source.depthSupport.reset();
    for(auto bit:{ReliabilityRejectBit::AlignmentInvalid,ReliabilityRejectBit::SampleInvalid,
        ReliabilityRejectBit::PatchRejected,ReliabilityRejectBit::NonFiniteEvidence}) {
        install(ReliabilityRejectMask(bit),1);check(consistent()==0,"Hard rejection acquired reconstruction support");
    }
    install(ReliabilityRejectMask(ReliabilityRejectBit::InsufficientPatchSupport),.01);
    check(std::abs(consistent()-.2)<1e-6,"Uncertain texture lost conservative support");
    install(0,1);source.scale*=2;check(consistent()==0,"Stale exposure reliability was accepted");
    source.scale/=2;source.scaleVariance+=.01;check(consistent()==0,"Stale exposure variance was accepted");
    source.scaleVariance-=.01;++source.motionRevision;check(consistent()==0,"Stale motion reliability was accepted");
    StampCaptureReliability(source);check(consistent()>0,"Current evidence was not restored");
    for(auto& cell:grid.nodes){cell.state=MotionNodeState::Rejected;cell.confidence=0;}
    check(!EvaluateCaptureMotion(source,{64,64},{},corrected),"Refinement resurrected rejected base motion");
    check(consistent()==0,"Stored reliability resurrected rejected base motion in reconstruction");
    check(Sr::MapCapture(source,64,64,true).z==1,"Fixed reference depended on alternate evidence");

    // Exercise the production refresh with the existing shifted CFA pyramids.
    for(auto& cell:grid.nodes){cell.state=MotionNodeState::Structured;cell.confidence=1;}
    source.refinedMotion.reset();source.frame.activeCfaPattern=Raw::CfaPattern::RGGB;
    PreparedSource fixed;fixed.frame=source.frame;
    PreparedDataset shape;shape.width=shape.height=128;
    auto refreshParameters=CaptureAlignmentParameters(shape);
    ProcessingRequest refreshRequest;refreshRequest.workerCount=2;refreshRequest.memoryBudgetBytes=64*1024*1024;
    alternate=pyramid(.35,-.2,false);std::string error;
    check(RefreshCaptureReliability(refreshRequest,fixed,source,refreshParameters,reference,alternate,{},error),error);
    const double before=EvaluateCaptureCorrespondence(source,{48,56}).confidence;
    auto correction=std::make_shared<MotionRefinementField>(*field);
    for(auto& cell:correction->cells){cell.delta={.7,-.4};cell.confidence=1;cell.covariance={.0025,0,.0025};}
    source.refinedMotion=correction;++source.motionRevision;
    check(!HasCurrentCaptureReliability(source),"Refinement retained the old evidence stamp");
    check(RefreshCaptureReliability(refreshRequest,fixed,source,refreshParameters,reference,alternate,{},error),error);
    check(HasCurrentCaptureReliability(source),"Refreshed reliability was not stamped");
    const auto refreshed=EvaluateCaptureCorrespondence(source,{48,56});
    check(refreshed.confidence>before+.2&&refreshed.confidence>.8,
        "Reliability was not measured against the corrected sampling coordinates");
    check(source.alignmentInspection&&source.alignmentDiagnostic.acceptedCoverage>0,
        "Refinement did not refresh its inspection evidence");
    const auto previous=source.reliability;
    refreshRequest.shouldCancel=[] {return true;};++source.motionRevision;
    check(!RefreshCaptureReliability(refreshRequest,fixed,source,refreshParameters,reference,alternate,{},error)&&
        source.reliability==previous&&!HasCurrentCaptureReliability(source),"Canceled refresh published partial reliability");
    std::cout<<"Burst alignment: independent subsets, flat fallback, subpixel improvement, color conflict and cancellation passed.\n";
    std::cout<<"Bracketing integration: shared rejection, revision checks and refined reliability passed.\n";
}
}
