#include "BracketingHdrColorValidation.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/SceneGradient.h"
#include "BracketingBorderValidation.h"
#include "BracketingBurstValidation.h"
#include "BracketingNoiseValidation.h"
#include "BracketingLocalDetailValidation.h"
#include "BracketingExposureValidation.h"
#include "BracketingDetailFusionValidation.h"
#include "BracketingSharpnessValidation.h"
#include "Raw/RawProcessingMath.h"
#include "Persistence/BracketingProject.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/ProjectStore.h"
#include "Raw/MultiFrame/PublicationFence.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <fstream>
#include <sstream>

namespace Stack::Validation {
namespace {
void Check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
Raw::RawImageData Capture(char id,double exposure,double noise=0,double iso=100,int width=128) {
    Raw::RawImageData raw;auto& m=raw.metadata;
    m.sourceContentSha256=std::string(64,id);m.sourceByteSize=width*128*2;m.sourcePath=std::string(1,id)+".dng";
    m.cameraMake="Stack";m.cameraModel="Bracket fixture";m.dngUniqueCameraModel=m.cameraModel;
    m.rawWidth=m.visibleWidth=width;m.rawHeight=m.visibleHeight=128;m.orientation=1;
    m.bitDepth=16;m.cfaPattern=Raw::CfaPattern::RGGB;m.pixelLayout=Raw::RawPixelLayout::MosaicBayer;m.mosaiced=true;m.isDng=true;
    m.blackLevel=1024;m.perChannelBlack.fill(1024);m.whiteLevel=65000;
    m.exposureTimeSeconds=static_cast<float>(exposure/iso);m.isoSpeed=static_cast<float>(iso);m.apertureFNumber=4;
    m.hasExposureTime=m.hasIsoSpeed=m.hasApertureFNumber=true;
    m.cameraWhiteBalance={1,1,1,1};m.hasDngNoiseProfile=true;
    m.dngNoiseProfile={{0,std::max(1e-10,noise*noise)},{0,std::max(1e-10,noise*noise)},{0,std::max(1e-10,noise*noise)}};
    raw.rawBuffer.resize(width*128);std::mt19937 rng(id);std::normal_distribution<double> random(0,noise);
    for(int y=0;y<128;++y) for(int x=0;x<width;++x) {
        const double scene=x<width*3/4?.15+.02*std::sin(x*.11):1.5;
        raw.rawBuffer[y*width+x]=static_cast<std::uint16_t>(std::clamp(std::llround(1024+63976*(scene*exposure+random(rng))),0ll,65000ll));
    }
    return raw;
}
double AlignmentScene(double x,double y) {
    const auto spot=[&](double cx,double cy,double radius,double amplitude) {
        const double dx=x-cx,dy=y-cy;return amplitude*std::exp(-(dx*dx+dy*dy)/(2*radius*radius));
    };
    return .27+.035*std::sin(x*.173)+.031*std::cos(y*.139)+.022*std::sin((x+y)*.091)+
        spot(27,35,8,.16)+spot(91,74,13,.11)-spot(49,101,7,.07)+
        (x>72&&x<108&&y>17&&y<45?.075:0.0)+(x>12&&x<39&&y>82&&y<112?.045:0.0);
}
Raw::RawImageData AlignedCapture(char id,double translationX,double translationY,double rotationDegrees=0,double exposure=1) {
    auto raw=Capture(id,exposure,0,100,128);const double radians=rotationDegrees*3.14159265358979323846/180.0;
    const double c=std::cos(radians),s=std::sin(radians),cx=63.5,cy=63.5;
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const double sx=x-cx-translationX,sy=y-cy-translationY;
        const double referenceX=c*sx+s*sy+cx,referenceY=-s*sx+c*sy+cy;
        const double scene=AlignmentScene(referenceX,referenceY);
        raw.rawBuffer[y*128+x]=static_cast<std::uint16_t>(std::clamp(std::llround(1024+63976*scene*exposure),0ll,65000ll));
    }
    return raw;
}
Raw::RawImageData LocallyWarpedCapture(char id) {
    auto raw=Capture(id,1,0,100,128);
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const double dx=2.5+1.8*std::sin(y*.065),dy=-1.5+1.4*std::sin(x*.057);
        const double scene=AlignmentScene(x-dx,y-dy);
        raw.rawBuffer[y*128+x]=static_cast<std::uint16_t>(std::clamp(std::llround(1024+63976*scene),0ll,65000ll));
    }
    return raw;
}
Raw::RawImageData LowLightDefocusedCapture(char id) {
    constexpr double noise=.0035;
    auto raw=Capture(id,1,noise,100,128);
    std::mt19937 rng(static_cast<unsigned>(id)*7919u);
    std::normal_distribution<double> random(0,noise);
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const double dx=x-70.,dy=y-59.;
        // Broad, low-contrast features model an intentionally defocused dark
        // background. They contain useful matching evidence but no crisp edge
        // that should be required before the burst can reduce noise.
        const double scene=.018+.006*std::exp(-(dx*dx+dy*dy)/(2*31.*31.))+
            .0025*std::sin(x*.035)+.002*std::cos(y*.031);
        raw.rawBuffer[y*128+x]=static_cast<std::uint16_t>(std::clamp(
            std::llround(1024+63976*(scene+random(rng))),0ll,65000ll));
    }
    return raw;
}
Raw::RawImageData CfaDisagreementCapture(char id) {
    auto raw=Capture(id,1,0,100,128);
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const auto site=((y&1)<<1)|(x&1);
        const std::array<std::array<double,2>,4> shift={{
            {{4.0,0.0}},{{7.0,3.0}},{{7.0,3.0}},{{6.0,0.0}}
        }};
        const double scene=AlignmentScene(x-shift[site][0],y-shift[site][1]);
        raw.rawBuffer[y*128+x]=static_cast<std::uint16_t>(std::clamp(
            std::llround(1024+63976*scene),0ll,65000ll));
    }
    return raw;
}
Raw::Bracketing::ProcessingRequest Request(const std::vector<Raw::RawImageData>& raws,const std::filesystem::path& path,bool grouped=false) {
    using namespace Raw::Bracketing;
    ProcessingRequest r;r.cacheDirectory=path;r.recipe.originFrameId="f0";r.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
    for(std::size_t i=0;i<raws.size();++i) {
        const auto id="f"+std::to_string(i);r.sources.push_back({id,raws[i].metadata.sourceContentSha256,raws[i].metadata.sourceByteSize,id});
        if(!grouped||i==0) r.recipe.groups.push_back({"g"+std::to_string(i),id,true,{}});
        r.recipe.groups.back().frames.push_back({id,true,false,0});
    }
    r.recipe.knots=EqualCurves(r.recipe.groups.size());
    r.decode=[raws](const std::filesystem::path& p,Raw::RawImageData& out,const auto&){out=raws.at(std::stoul(p.string().substr(1)));return true;};return r;
}
}
bool ValidateBracketing() {
    using namespace Raw::Bracketing;
    const auto directory=std::filesystem::temp_directory_path()/("stack-bracketing-validation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        ValidateBracketingHdrColor();
        ValidateBracketingBorders();
        ValidateBracketingBurstAlignment();
        ValidateBracketingNoiseEvidence();
        ValidateBracketingLocalDetailEvidence();
        ValidateBracketingLocalDetailPipeline(directory/"local-detail",Capture,Request);
        ValidateBracketingLocalExposure();
        ValidateBracketingDetailFusion();
        ValidateBracketingCaptureSharpness(directory/"sharpness",Capture,Request);
        auto curves=EqualCurves(4);InsertKnot(curves,-2);Redistribute(curves[1].share,2,.7);Redistribute(curves[1].left,2,.9);Redistribute(curves[1].right,2,.4);
        for(int i=0;i<2000;++i) {double total=0;for(auto w:Evaluate(curves,-14+i*.012)) {Check(w>=0&&w<=1,"Curve bounds");total+=w;}Check(std::abs(total-1)<1e-10,"Bezier sum");}
        auto split=curves;InsertKnot(split,1);
        for(int i=0;i<100;++i) {auto a=Evaluate(curves,-12+i*.2),b=Evaluate(split,-12+i*.2);for(unsigned g=0;g<4;++g) Check(std::abs(a[g]-b[g])<1e-8,"Insertion changed curve shape");}
        BracketingRecipe recipe;recipe.originFrameId="a";recipe.groups={{"1","A",true,{{"a"}}},{"2","B",true,{{"b"}}}};recipe.knots=EqualCurves(2);
        std::string error;BracketingRecipe restored;Check(Deserialize(Serialize(recipe),restored,error),error);Check(Identity(recipe)==Identity(restored),"Recipe round trip");
        auto legacy=Serialize(recipe);legacy["version"]=1;legacy.erase("alignmentMode");
        Check(Deserialize(legacy,restored,error)&&restored.alignmentMode==AlignmentMode::FixedCoordinates,"Version-1 bracket alignment migration failed");
        auto version2=Serialize(recipe);version2["version"]=2;version2["alignmentMode"]="automatic-global";
        Check(Deserialize(version2,restored,error)&&restored.alignmentMode==AlignmentMode::AutomaticGlobal,
            "Version-2 bracket changed its saved global alignment mode");
        Check(restored.groups[0].colorSlot!=restored.groups[1].colorSlot,"Group colors collide");
        const int survivor=restored.groups[1].colorSlot;restored.groups.erase(restored.groups.begin());
        Check(ColorSlots(restored)[0]==survivor,"Removing a group changed the surviving color");
        auto bad=Serialize(recipe);bad["version"]=99;Check(!Deserialize(bad,restored,error),"Unknown version accepted");
        recipe.groups[1].frames[0].id="a";Check(!Validate(recipe,error),"Duplicate frame accepted");recipe.groups[1].frames[0].id="b";
        auto blend=Blend({{.2,.04,1,1,.2,1,true,false},{.8,.16,1,1,.8,1,true,false}},{.7,.3},recipe);
        Check(std::abs(blend.value-.38)<1e-12,"Requested shares were reweighted by variance");
        {
            Observation first{.2,.04,1,1,.2,1,true,false},second{.8,.16,1,1,.8,1,true,false};
            first.measurementVariance=.01;first.uncertaintyVariance=.03;
            second.measurementVariance=.04;second.uncertaintyVariance=.12;
            const auto separated=Blend({first,second},{.7,.3},recipe);
            Check(std::abs(separated.measurementVariance-.0085)<1e-12&&
                std::abs(separated.variance-separated.measurementVariance-separated.uncertaintyVariance)<1e-12,
                "Registration/model risk leaked into the measurement noise used for residual denoising");
        }
        blend=Blend({{.2,.04,1,1,.2,1,true,false},{0,0,0,0,1,4,true,true}},{0,1},recipe);
        Check(blend.valid&&blend.fallback&&blend.actual[0]==1,"Clipped requested-source fallback");
        blend=Blend({{0,0,0,0,1,1,true,true},{0,0,0,0,4,4,true,true}},{.5,.5},recipe);
        Check(!blend.valid&&blend.clipped&&blend.value==1&&
            blend.fallbackReason==MeasurementFallbackReason::ShortestExposure,
            "All-clipped shortest exposure fallback");
        blend=Blend({{},{}},{.5,.5},recipe);Check(!blend.valid&&std::isfinite(blend.value),"Invalid data produced invalid numeric output");
        auto a=Capture('a',1,.002),b=Capture('b',1,.002);
        auto request=Request({a,b},directory/"denoise",true);auto denoised=Process(request);
        Check(denoised.status==BracketingResult::Status::Completed,denoised.message);
        {
            auto uncalibratedA=Capture('c',1,.0035,100,512);
            auto uncalibratedB=Capture('d',1,.0035,100,512);
            uncalibratedA.metadata.hasDngNoiseProfile=uncalibratedB.metadata.hasDngNoiseProfile=false;
            auto measuredRequest=Request({uncalibratedA,uncalibratedB},directory/"temporal-noise-calibration",true);
            auto measured=Process(measuredRequest);
            Check(measured.status==BracketingResult::Status::Completed,measured.message);
            const auto& noise=measured.analysis->prepared->sources[0].noise;
            Check(noise.diagnostics.source=="temporal-burst-haar-v1","Uncalibrated burst did not use temporal noise evidence");
            const double variance=noise.sites[0].shotScale*.15+noise.sites[0].offsetVariance;
            Check(std::abs(variance/(.0035*.0035)-1)<.35,"Temporal noise estimate did not recover real burst variance");
        }
        std::vector<std::vector<Observation>> temporal;Raw::Mfd::PreparedRawTile temporalOrigin;
        Check(ReadTemporalGroups(request,*denoised.analysis->prepared,0,0,temporal,temporalOrigin,error),error);
        double before=0,after=0,publishedError=0,bias=0;std::size_t samples=0;
        for(int y=0;y<128;++y) for(int x=0;x<96;++x) {const auto p=y*128+x;const double truth=.15+.02*std::sin(x*.11);
            const double e=(a.rawBuffer[p]-1024.)/63976-truth,d=temporal[0][p].value-truth;
            const double published=(*denoised.raw->normalizedMosaicBuffer)[p]-truth;
            Check(denoised.raw->normalizedMosaicBuffer->at(p)==static_cast<float>(temporal[0][p].value),
                "Bracketing applied spatial cleanup to the temporal merge");
            Check(denoised.raw->multiFrameMeasurementSidecars->variance->at(p)==static_cast<float>(temporal[0][p].measurementVariance),
                "Bracketing published spatially reduced variance instead of temporal variance");
            before+=e*e;after+=d*d;publishedError+=published*published;bias+=published;++samples;}
        Check(after/before>.4&&after/before<.65,"Two-frame noise variance did not approach half");
        Check(std::abs(publishedError-after)<after*.0001,"Published detail differs from temporal fusion");
        Check(std::abs(bias/samples)<.00015,"Denoise changed mean exposure");
        Check(denoised.raw->multiFrameMeasurementSidecars->effectiveSupport->at(32)>1.9,"Group sample support lost");
        request.analysis=denoised.analysis;
        auto standardAgain=Process(request);
        Check(standardAgain.status==BracketingResult::Status::Completed,standardAgain.message);
        Check(*standardAgain.raw->normalizedMosaicBuffer==*denoised.raw->normalizedMosaicBuffer&&
            *standardAgain.raw->multiFrameMeasurementSidecars->variance==*denoised.raw->multiFrameMeasurementSidecars->variance&&
            *standardAgain.raw->multiFrameMeasurementSidecars->effectiveSupport==*denoised.raw->multiFrameMeasurementSidecars->effectiveSupport&&
            *standardAgain.raw->multiFrameMeasurementSidecars->fusionUncertaintyVariance==*denoised.raw->multiFrameMeasurementSidecars->fusionUncertaintyVariance,
            "Cached Standard output changed temporal measurements");
        ReconstructionMode removedMode;
        Check(!ParseReconstructionMode("experimental-standard",removedMode),"Removed spatial mode remains available");
        auto darkA=Capture('0',1),darkB=Capture('1',1);
        std::fill(darkA.rawBuffer.begin(),darkA.rawBuffer.end(),1344);
        std::fill(darkB.rawBuffer.begin(),darkB.rawBuffer.end(),1344);
        auto darkDenoise=Process(Request({darkA,darkB},directory/"dark-denoise-metadata",true));
        Check(darkDenoise.status==BracketingResult::Status::Completed,darkDenoise.message);
        Check(std::abs(darkDenoise.raw->normalizedMosaicBuffer->at(32)-320.0/63976.0)<1e-6,
            "Matching-setting dark denoise burst changed its metadata exposure scale");
        auto alignedRequest=Request({AlignedCapture('1',0,0),AlignedCapture('2',4,-2,.35)},directory/"alignment",true);
        alignedRequest.recipe.alignmentMode=AlignmentMode::AutomaticGlobal;
        auto aligned=Process(alignedRequest);Check(aligned.status==BracketingResult::Status::Completed,aligned.message);
        const auto alignment=aligned.analysis->alignments.at("f1");
        Check(alignment.accepted&&std::abs(alignment.translationX-4)<1.0&&std::abs(alignment.translationY+2)<1.0,"Automatic RAW translation estimate is incorrect");
        Check(alignment.model==CaptureAlignment::Model::GlobalAffine,"Rotation refinement did not produce a global transform");
        Check(std::abs(alignment.rotationDegrees-.35)<.25,"Automatic RAW rotation estimate is incorrect");
        // A displaced HDR pair has different clipped regions. Sharing their
        // clipping mask before registration used to create a false zero shift.
        auto movedHdr=Request({AlignedCapture('a',0,0,0,3.125),AlignedCapture('b',8,-4)},
            directory/"moved-clipped-hdr");
        movedHdr.recipe.alignmentMode=AlignmentMode::AutomaticGlobal;
        const auto movedResult=Process(movedHdr);
        Check(movedResult.status==BracketingResult::Status::Completed,movedResult.message);
        const auto movedAlignment=movedResult.analysis->alignments.at("f1");
        Check(std::abs(movedAlignment.translationX-8)<1&&std::abs(movedAlignment.translationY+4)<1,
            "Clipped HDR highlights caused a false stationary alignment");
        Check(std::abs(movedResult.analysis->calibratedEv.at("f1")+std::log2(3.125))<.05,
            "Moved HDR captures did not retain measured exposure calibration");
        auto globalPlaneDisagreementRequest=Request(
            {AlignedCapture('0',0,0),CfaDisagreementCapture('1')},
            directory/"global-alignment-plane-disagreement",true);
        globalPlaneDisagreementRequest.recipe.alignmentMode=AlignmentMode::AutomaticGlobal;
        const auto globalPlaneDisagreement=Process(globalPlaneDisagreementRequest);
        Check(globalPlaneDisagreement.status==BracketingResult::Status::Completed,
            globalPlaneDisagreement.message);
        auto planeDisagreementRequest=Request(
            {AlignedCapture('0',0,0),CfaDisagreementCapture('1')},
            directory/"local-alignment-plane-disagreement",true);
        planeDisagreementRequest.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
        planeDisagreementRequest.workerCount=2;
        const auto planeDisagreement=Process(planeDisagreementRequest);
        Check(planeDisagreement.status==BracketingResult::Status::Completed,
            planeDisagreement.message);
        Check(planeDisagreement.analysis->alignments.at("f1").localApplied,
            "CFA-plane disagreement aborted Automatic local before reliability analysis");
        double alignmentError=0;std::size_t alignmentSamples=0;
        for(int y=12;y<116;y+=3) for(int x=12;x<116;x+=3) {
            alignmentError+=std::abs(aligned.raw->normalizedMosaicBuffer->at(y*128+x)-AlignmentScene(x,y));++alignmentSamples;
        }
        Check(alignmentError/alignmentSamples<.006,"CFA-aware aligned merge does not reproduce the reference scene");
        auto localRequest=Request({AlignedCapture('3',0,0),LocallyWarpedCapture('4')},directory/"local-alignment",true);
        localRequest.recipe.alignmentMode=AlignmentMode::AutomaticLocal;localRequest.workerCount=2;
        auto local=Process(localRequest);Check(local.status==BracketingResult::Status::Completed,local.message);
        const auto localAlignment=local.analysis->alignments.at("f1");
        Check(localAlignment.localApplied,"Automatic local mode did not build local alignment evidence");
        Check(local.analysis->alignmentInspection.count("f1")!=0,"Local alignment confidence view is missing");
        Check(local.raw->multiFrameMeasurementSidecars->localRejection&&
            local.raw->multiFrameMeasurementSidecars->fallbackReason&&
            local.raw->multiFrameMeasurementSidecars->localRejection->size()==128u*128u&&
            local.raw->multiFrameMeasurementSidecars->fallbackReason->size()==128u*128u,
            "Local rejection and fallback measurement sidecars are missing");
        double localError=0;std::size_t localSamples=0;
        for(int y=16;y<112;y+=3) for(int x=16;x<112;x+=3) {
            localError+=std::abs(local.raw->normalizedMosaicBuffer->at(y*128+x)-AlignmentScene(x,y));++localSamples;
        }
        Check(localError/localSamples<.012,"Local alignment did not protect a spatially warped static scene");
        auto supportedLocalRequest=Request(
            {AlignedCapture('5',0,0),AlignedCapture('6',2,-2)},
            directory/"local-alignment-support",true);
        supportedLocalRequest.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
        supportedLocalRequest.workerCount=2;
        auto supportedLocal=Process(supportedLocalRequest);
        Check(supportedLocal.status==BracketingResult::Status::Completed,
            supportedLocal.message);
        Check(supportedLocal.analysis->alignments.at("f1").acceptedCoverage>0,
            "Automatic local mode rejected every trustworthy alternate measurement");
        Check(std::any_of(
            supportedLocal.raw->multiFrameMeasurementSidecars->effectiveSupport->begin(),
            supportedLocal.raw->multiFrameMeasurementSidecars->effectiveSupport->end(),
            [](float support){return support>1.000001f;}),
            "Automatic local mode claimed no multi-frame support in reliable regions");
        std::vector<Raw::RawImageData> lowLightFrames;
        for(char id='0';id<'8';++id) lowLightFrames.push_back(LowLightDefocusedCapture(id));
        auto lowLightRequest=Request(lowLightFrames,directory/"local-low-light-defocused",true);
        lowLightRequest.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
        lowLightRequest.workerCount=2;
        auto lowLight=Process(lowLightRequest);
        Check(lowLight.status==BracketingResult::Status::Completed,lowLight.message);
        const auto& lowLightSupport=*lowLight.raw->multiFrameMeasurementSidecars->effectiveSupport;
        std::vector<float> interiorSupport;
        for(int y=16;y<112;++y) for(int x=16;x<112;++x)
            interiorSupport.push_back(lowLightSupport[y*128+x]);
        std::sort(interiorSupport.begin(),interiorSupport.end());
        Check(interiorSupport[interiorSupport.size()/2]>2.0f,
            "Automatic local reduced a defocused low-light burst to sparse denoise islands");
        const auto broadlySupported=std::count_if(interiorSupport.begin(),interiorSupport.end(),
            [](float support){return support>2.0f;});
        Check(broadlySupported>static_cast<std::ptrdiff_t>(interiorSupport.size()*3/5),
            "Automatic local did not preserve burst support across the low-light frame");
        // Support alone can pass while the reference still dominates. Measure
        // error against the noiseless scene, including any alignment bias.
        for(const unsigned frameCount:{5u,10u}) {
            std::vector<Raw::RawImageData> burst;
            for(unsigned i=0;i<frameCount;++i)
                burst.push_back(LowLightDefocusedCapture("0123456789"[i]));
            auto measured=Request(burst,directory/("measured-low-light-"+std::to_string(frameCount)),true);
            measured.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
            measured.workerCount=2;
            for(auto& frame:measured.recipe.groups.front().frames) {
                frame.manualExposure=true;frame.relativeEv=0;
            }
            const auto merged=Process(measured);
            Check(merged.status==BracketingResult::Status::Completed,merged.message);
            double inputError=0,outputError=0,bias=0;unsigned samples=0;
            for(int y=16;y<112;++y) for(int x=16;x<112;++x) {
                const double dx=x-70.,dy=y-59.;
                const double truth=.018+.006*std::exp(-(dx*dx+dy*dy)/(2*31.*31.))+
                    .0025*std::sin(x*.035)+.002*std::cos(y*.031);
                const auto p=y*128+x;
                const double before=(burst.front().rawBuffer[p]-1024.)/63976.-truth;
                const double after=merged.raw->normalizedMosaicBuffer->at(p)-truth;
                inputError+=before*before;outputError+=after*after;bias+=after;++samples;
            }
            const double ratio=outputError/inputError;
            std::cout<<"Local low-light "<<frameCount<<" frames: MSE ratio "<<ratio
                <<", bias "<<bias/samples<<'\n';
            Check(ratio<2.0/frameCount,"Local alignment retained too much noise in a static low-light burst");
            Check(std::abs(bias/samples)<.0005,"Local low-light merge changed the mean scene signal");
        }
        auto fixedLowLightRequest=Request(lowLightFrames,directory/"fixed-low-light-stable-pilot",true);
        for(auto& frame:fixedLowLightRequest.recipe.groups.front().frames)
            if(frame.id!=fixedLowLightRequest.recipe.originFrameId) {
                frame.manualExposure=true;frame.relativeEv=0;
            }
        auto fixedLowLight=Process(fixedLowLightRequest);
        Check(fixedLowLight.status==BracketingResult::Status::Completed,fixedLowLight.message);
        double maximumMeanError=0;
        Check(ReadTemporalGroups(fixedLowLightRequest,*fixedLowLight.analysis->prepared,0,0,temporal,temporalOrigin,error),error);
        for(std::size_t p=0;p<lowLightFrames.front().rawBuffer.size();++p) {
            double expected=0;
            for(const auto& frame:lowLightFrames)
                expected+=(frame.rawBuffer[p]-1024.)/63976./lowLightFrames.size();
            maximumMeanError=std::max(maximumMeanError,std::abs(
                temporal[0][p].value-expected));
        }
        Check(maximumMeanError<2e-7,
            "A fixed-coordinate dark burst was not the arithmetic mean of equal-noise captures: "+
            std::to_string(maximumMeanError));
        auto duplicate=Request({a,a},directory/"duplicate",true);Check(Process(duplicate).status==BracketingResult::Status::Failed,"Duplicate data claimed independent evidence");
        auto hdrRequest=Request({Capture('c',1),Capture('d',.25,0,800),Capture('e',.5)},directory/"hdr");
        auto hdr=Process(hdrRequest);Check(hdr.status==BracketingResult::Status::Completed,hdr.message);
        Check(std::abs((*hdr.raw->normalizedMosaicBuffer)[110]-1.5)<.002,"HDR highlight recovery or mixed ISO scale failed");
        Check(hdr.raw->normalizedMosaicInputContract==Raw::NormalizedMosaicInputContract::BracketingPreGain,"Mosaic contract lost");
        {
            auto observed=hdrRequest;observed.analysis=hdr.analysis;
            std::atomic<unsigned> events{0};std::atomic<bool> wrongStage{false};
            observed.reportPresentation=[&](const ProcessingProgress& event){
                ++events;if(event.stage!=ProcessingStage::Blend)wrongStage=true;
            };
            const auto replay=Process(observed);
            Check(replay.status==BracketingResult::Status::Completed,"Observed processing failed");
            Check(events>0&&!wrongStage,"Cached curve processing replayed source preparation stages");
            Check(*replay.raw->normalizedMosaicBuffer==*hdr.raw->normalizedMosaicBuffer,"Presentation changed output pixels");
        }
        auto edited=hdrRequest;edited.analysis=hdr.analysis;edited.recipe.automatic=false;
        auto second=Process(edited);Check(second.status==BracketingResult::Status::Completed,second.message);
        Check(second.analysis->prepared==hdr.analysis->prepared,"Curve edit decoded sources again");
        const auto preview=ReblendPreview(hdr.preview,edited.recipe,*hdr.analysis);
        Check(preview.resultRgb.size()==second.preview.resultRgb.size(),"Preview sizes differ");
        for(std::size_t i=0;i<preview.resultRgb.size();++i)Check(std::abs(preview.resultRgb[i]-second.preview.resultRgb[i])<1e-6,"Preview and full merge diverged");
        auto tiny=hdrRequest;tiny.memoryBudgetBytes=1024;Check(Process(tiny).status==BracketingResult::Status::Failed,"Memory budget ignored");
        auto cancel=hdrRequest;cancel.shouldCancel=[] {return true;};Check(Process(cancel).status==BracketingResult::Status::Canceled,"Cancellation ignored");
        auto incompatible=Request({Capture('f',1),Capture('1',1,0,100,130)},directory/"incompatible");
        Check(Process(incompatible).status==BracketingResult::Status::Failed,"Mismatched sensor grid accepted");
        auto focusA=Capture('8',1),focusB=Capture('9',.5);
        focusA.metadata.focusDistanceMeters=.566f;focusB.metadata.focusDistanceMeters=.653f;
        auto focusResult=Process(Request({focusA,focusB},directory/"focus"));
        Check(focusResult.status==BracketingResult::Status::Completed,"Different focus distances were rejected");
        Check(std::any_of(focusResult.analysis->diagnostics.begin(),focusResult.analysis->diagnostics.end(),
            [](const auto& message){return message.find("Focus distances differ")!=std::string::npos;}),
            "Different focus distances did not produce a warning");
        auto singleInput=Capture('2',1);auto one=Request({singleInput},directory/"single");auto single=Process(one);Check(single.status==BracketingResult::Status::Completed,single.message);
        Check(std::abs((*single.raw->normalizedMosaicBuffer)[32]-(singleInput.rawBuffer[32]-1024.)/63976) < 1e-6,"Single input changed scale");
        recipe.groups[1].enabled=false;ConstrainEnabledCurves(recipe);
        for(int i=0;i<100;++i) {const auto shares=Evaluate(recipe.knots,-12+i*.2);Check(std::abs(shares[0]-1)<1e-12&&shares[1]==0,"Exclusion did not redistribute curves");}
        auto excluded=hdrRequest;excluded.recipe.groups[0].enabled=false;
        auto exclusion=Process(excluded);Check(exclusion.status==BracketingResult::Status::Completed,exclusion.message);
        Check(std::abs((*exclusion.raw->normalizedMosaicBuffer)[110]-1.5)<.002,"Excluding origin moved exposure scale");
        auto negative=Capture('3',1);negative.rawBuffer[16]=900;
        auto signedResult=Process(Request({negative},directory/"negative"));Check(signedResult.status==BracketingResult::Status::Completed,signedResult.message);
        Check(signedResult.raw->normalizedMosaicBuffer->at(16)<0&&signedResult.raw->multiFrameMeasurementSidecars->validity->at(16),"Negative measurement was lost");
        auto noOverlap=Capture('4',.0001);auto disconnected=Request({Capture('5',1),noOverlap},directory/"disconnected");
        disconnected.sources[1].displayName="Underexposed capture.dng";
        const auto disconnectedResult=Process(disconnected);
        Check(disconnectedResult.status==BracketingResult::Status::Failed,"Uncalibrated exposure was accepted");
        Check(disconnectedResult.message.find("Underexposed capture.dng")!=std::string::npos,
            "Exposure failure did not identify the original capture filename");
        disconnected.recipe.groups[1].frames[0].manualExposure=true;disconnected.recipe.groups[1].frames[0].relativeEv=std::log2(.0001);
        Check(Process(disconnected).status==BracketingResult::Status::Completed,"Manual scale could not resolve missing overlap");
        auto detailRequest=hdrRequest;detailRequest.analysis=hdr.analysis;
        auto detail=RenderNativeDetail(detailRequest,64,64,120);Check(detail.width>0,"Native detail missing");
        Check(detail.originals.size()==3&&hdr.analysis->originals.size()==3,"Individual originals missing");
        const auto& shortOriginal=*detail.originals.at("f1");
        Check(std::abs(shortOriginal.exposureScale-4)<.01,"Original exposure matching scale incorrect");
        Check(shortOriginal.rgb[30*3]<detail.sourceRgb[(30*3+1)*3]*.26f,"Original was silently exposure normalized");
        auto denoiseDetailRequest=request;denoiseDetailRequest.analysis=denoised.analysis;
        const auto noiseDetail=RenderNativeDetail(denoiseDetailRequest,64,64,120);
        Check(noiseDetail.originals.size()==2,"Denoise originals were replaced by group averages");
        std::vector<float> rawOriginal(a.rawBuffer.size());
        for(std::size_t i=0;i<rawOriginal.size();++i)rawOriginal[i]=(a.rawBuffer[i]-1024.f)/63976.f;
        for(unsigned y=8;y<100;y+=7)for(unsigned x=8;x<80;x+=7) {
            const auto truth=Raw::Processing::DemosaicMalvarHeCutlerAt(rawOriginal,128,128,Raw::CfaPattern::RGGB,x+4,y+4);
            for(unsigned c=0;c<3;++c)Check(std::abs(noiseDetail.originals.at("f0")->rgb[(y*120+x)*3+c]-truth[c])<1e-6,"Native original differs from full-resolution demosaic at matching coordinates");
        }
        auto wide=Request({Capture('6',1,0,100,1024),Capture('7',.25,0,100,1024)},directory/"tiles");
        auto tiled=Process(wide);Check(tiled.status==BracketingResult::Status::Completed,tiled.message);
        for(int x=504;x<520;++x)Check(std::abs(tiled.raw->normalizedMosaicBuffer->at(x)-(.15+.02*std::sin(x*.11)))<.0001,"Tile boundary changed sample coordinates or signal");
        auto tiledDetailRequest=wide;tiledDetailRequest.analysis=tiled.analysis;
        const auto tiledDetail=RenderNativeDetail(tiledDetailRequest,512,64,128);
        Check(tiledDetail.width>0,"Native detail across a group-cache tile boundary is missing");
        for(unsigned y=4;y+4<tiledDetail.height;y+=7) for(unsigned x=4;x+4<tiledDetail.width;x+=5) {
            const auto full=Raw::Processing::DemosaicMalvarHeCutlerAt(*tiled.raw->normalizedMosaicBuffer,
                1024,128,Raw::CfaPattern::RGGB,x+static_cast<unsigned>(tiledDetail.sensorOriginX),
                y+static_cast<unsigned>(tiledDetail.sensorOriginY));
            for(unsigned c=0;c<3;++c) Check(std::abs(tiledDetail.resultRgb[(y*tiledDetail.width+x)*3+c]-full[c])<1e-6,
                "Native temporal output differs from the full evaluated mosaic");
        }
        tiledDetailRequest.recipe.groups[0].enabled=false;
        Check(RenderNativeDetail(tiledDetailRequest,512,64,128).width==0,
            "Native detail accepted analysis from a different input arrangement");
        {
            auto layoutRequest=Request({Capture('a',1,.002,100,1280),Capture('b',1,.002,100,1280)},
                directory/"memory-layout",true);
            layoutRequest.memoryBudgetBytes=2ull*1024*1024*1024;
            const auto largerTiles=Process(layoutRequest);
            Check(largerTiles.status==BracketingResult::Status::Completed,largerTiles.message);
            layoutRequest.memoryBudgetBytes=192ull*1024*1024;
            const auto smallerTiles=Process(layoutRequest);
            Check(smallerTiles.status==BracketingResult::Status::Completed,smallerTiles.message);
            Check(largerTiles.analysis->prepared->sources[0].frame.tileRawPixels!=
                  smallerTiles.analysis->prepared->sources[0].frame.tileRawPixels,
                  "Memory-layout fixture did not exercise different tile geometries");
            Check(*largerTiles.raw->normalizedMosaicBuffer==*smallerTiles.raw->normalizedMosaicBuffer,
                  "Reopening with a different tile/memory budget reused mislocated group cache data");
        }
        {
            auto ramp=Capture('8',1,0,100,1024);
            for(int y=0;y<128;++y) for(int x=0;x<1024;++x)
                ramp.rawBuffer[y*1024+x]=static_cast<std::uint16_t>(1024+std::llround(63976*(.1+x*.0002+y*.0001)));
            auto guideRequest=Request({ramp},directory/"scene-gradient-halo",true);
            PreparedDataset prepared;std::vector<std::string> diagnostics;
            Check(Prepare(guideRequest,prepared,diagnostics,error),error);
            Raw::Mfd::DirectoryNormalizedTileCache cache(prepared.directory);
            const auto& frame=prepared.sources[prepared.origin].frame;
            for(unsigned tx=0;tx<frame.tileColumns;++tx) {
                Raw::Mfd::PreparedRawTile tile;
                Check(Raw::Mfd::ReadPreparedTile(frame,cache,tx,0,tile,&error)==Raw::Mfd::TileCacheReadStatus::Hit,error);
                std::vector<Raw::Mfd::RawSignalGradient> gradient;
                Check(BuildSceneGradients(guideRequest,prepared,tile,gradient,error),error);
                for(unsigned x=0;x<tile.extent.width;++x) {
                    if(tile.originX+x<8||tile.originX+x>=1016) continue;
                    const auto& g=gradient[32*tile.extent.width+x];
                    Check(std::abs(g.dxPerRawPixel-.0002)<2e-6&&std::abs(g.dyPerRawPixel-.0001)<2e-6,
                        "Scene gradient changed at a tile boundary or mixed CFA coordinates");
                }
            }
        }
        auto corrupt=hdrRequest;corrupt.analysis=hdr.analysis;
        // A derived group tile is disposable. Corruption must rebuild from
        // verified source measurements without changing the result or analysis.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(
            corrupt.analysis->prepared->directory/"group-measurements-v3"))
            if(entry.is_regular_file()) std::ofstream(entry.path(),std::ios::binary|std::ios::trunc)<<"corrupt";
        const auto rebuiltGroups=Process(corrupt);
        Check(rebuiltGroups.status==BracketingResult::Status::Completed,rebuiltGroups.message);
        Check(rebuiltGroups.analysis->prepared==hdr.analysis->prepared&&
              *rebuiltGroups.raw->normalizedMosaicBuffer==*hdr.raw->normalizedMosaicBuffer,
              "Corrupt group cache did not rebuild identically from prepared captures");
        // Prepared source corruption cannot be concealed by a derived cache.
        // In particular, the origin is needed for the RAW calibration contract.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(corrupt.analysis->prepared->directory))
            if(entry.is_regular_file()&&entry.path().string().find("group-measurements-v3")==std::string::npos)
                std::ofstream(entry.path(),std::ios::binary|std::ios::trunc)<<"corrupt";
        Check(Process(corrupt).status==BracketingResult::Status::Failed,"Corrupt prepared cache was accepted");
        corrupt.analysis.reset();auto recovered=Process(corrupt);Check(recovered.status==BracketingResult::Status::Completed,recovered.message);
        Check(*recovered.raw->normalizedMosaicBuffer==*hdr.raw->normalizedMosaicBuffer,"Cache recovery changed result");

        using namespace Stack::Project;
        RawProjectSnapshot bootstrap;bootstrap.projectId=GenerateStableUuid();bootstrap.projectName="Bracket persistence validation";
        auto store=CreateProjectStore(directory/"project",ProjectStorageKind::DirectoryBundle,bootstrap);Check(static_cast<bool>(store),store.message);
        auto snapshot=store.snapshot;auto transaction=store.store->BeginTransaction(snapshot.persistedStorageRevision);
        MultiFrameSourceSet set;set.sourceSetId=GenerateStableUuid();set.name="Bracket";set.inputFamily=MultiFrameInputFamily::Raw;
        for(int i=0;i<3;++i) {
            const auto path=directory/("capture"+std::to_string(i)+".dng");std::ofstream(path,std::ios::binary)<<"synthetic asset "<<i;
            EmbeddedAssetRecord asset;auto metadata=Capture(static_cast<char>('a'+i),i==2?.25:1).metadata;
            Check(store.store->StageAssetFile(transaction,path,MultiFrameInputFamily::Raw,SerializeRawCaptureCompatibilitySummary(BuildRawCaptureCompatibilitySummary(metadata)),asset,&error),error);
            snapshot.embeddedAssets.push_back(asset);SourceSetFrame frame;frame.frameId=GenerateStableUuid();frame.assetId=asset.assetId;set.frames.push_back(frame);
        }
        InitializeBracketing(set,snapshot);Check(set.settings["bracketing"]["groups"].size()==2,"Automatic exposure grouping failed");
        Check(set.settings["algorithmVersion"]==RecipeVersion&&set.settings["bracketing"]["alignmentMode"]=="automatic-local","New brackets do not enable automatic local alignment");
        set.settings["sharedPostHdrRecipe"]={{"sentinelRawEdit",1.25}};
        snapshot.sourceSets.push_back(set);snapshot.activeSourceSetId=set.sourceSetId;snapshot.activeFrameId=set.referenceFrameId;
        const auto validation=ValidateRawProjectSnapshot(snapshot);Check(validation.valid,validation.errors.empty()?"Invalid bracket snapshot":validation.errors.front());
        auto commit=store.store->Commit(transaction,snapshot);Check(static_cast<bool>(commit),commit.message);snapshot.persistedStorageRevision=commit.committedStorageRevision;
        auto portable=ConvertProjectStore(store.store,snapshot,directory/"portable.stack",ProjectStorageKind::PortableFile);Check(static_cast<bool>(portable),portable.message);
        auto reopened=OpenProjectStore(directory/"portable.stack");Check(static_cast<bool>(reopened),reopened.message);
        Check(reopened.snapshot.sourceSets.front().settings==set.settings,"Portable reopen changed bracket or RAW edits");
        Check(reopened.store->Verify(reopened.snapshot),"Portable bracket lost owned originals");
        Check(reopened.snapshot.multiFrameGraph.nodes.empty(),"Reopen created a competing legacy graph");
        auto savedSnapshot=reopened.snapshot;
        auto storedRecipe=hdrRequest.recipe;
        storedRecipe.originFrameId=set.frames.front().frameId;
        for(std::size_t g=0;g<storedRecipe.groups.size();++g)
            storedRecipe.groups[g].frames.front().id=set.frames[g].frameId;
        savedSnapshot.sourceSets.front().settings["bracketing"]=Serialize(storedRecipe);
        auto storedResult=hdr;
        Check(!storedResult.preview.samples.empty(),"The saved observation fixture is empty");
        for(std::size_t i=0;i<storedResult.preview.samples.size();++i)
            storedResult.preview.samples[i].fallbackClipped=(i%2)==0;
        Check(SaveBracketingResult(reopened.store,savedSnapshot,set.sourceSetId,storedResult,{},error),error);
        BracketingResult loadedResult;
        Check(RestoreBracketingResult(reopened.store,savedSnapshot,set.sourceSetId,loadedResult,error),error);
        Check(loadedResult.preview.samples.size()==storedResult.preview.samples.size(),"Saved observation count changed");
        for(std::size_t i=0;i<loadedResult.preview.samples.size();++i)
            Check(loadedResult.preview.samples[i].fallbackClipped==storedResult.preview.samples[i].fallbackClipped,
                "Saving and reopening lost fallback capture clipping");
        std::error_code ec;std::filesystem::remove_all(directory,ec);
        std::cout<<"Bracketing curves, RAW alignment, noise averaging, clipping, mixed ISO, preview parity, persistence, identity, cancellation and memory checks passed.\n";return true;
    }catch(const std::exception& e) {std::cerr<<"Bracketing validation failed: "<<e.what()<<"\nFixtures: "<<directory<<'\n';return false;}
}
} // namespace Stack::Validation
