#include "ProcessingInternal.h"
#include "BurstAlignment.h"
#include "PresentationEvidence.h"
#include "BorderSupport.h"
#include "PhotometricReliability.h"
#include "ExposureRefinement.h"

#include "Raw/MultiFrameDenoise/GpuLocalMotion.h"
#include "GpuMotionSession.h"
#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace Raw::Bracketing {
namespace {

constexpr std::array<Mfd::CfaSite,4> kSites={
    Mfd::CfaSite::Red,Mfd::CfaSite::Green0,
    Mfd::CfaSite::Green1,Mfd::CfaSite::Blue};

std::size_t SiteIndex(Mfd::CfaSite site) {
    return static_cast<std::size_t>(site);
}

bool HardInvalid(std::uint8_t flags) {
    return Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||
        Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip)||
        Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective)||
        Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::DecoderRepaired);
}

bool HardReliabilityRejection(std::uint16_t bits) {
    constexpr auto mask=
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::AlignmentInvalid)|
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::SampleInvalid)|
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::PatchRejected)|
        Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::NonFiniteEvidence);
    return (bits&mask)!=0;
}

std::size_t AddConservativeGlobalFallbackNodes(
    Mfd::LocalMotionGrid& grid,bool globalAccepted,const Mfd::Parameters& parameters) {
    std::size_t converted=0;
    const double sigma=std::max(2.0,parameters.registration.flatSafeCovarianceRawPixels);
    for(auto& node:grid.nodes) {
        if(node.state!=Mfd::MotionNodeState::Rejected) continue;
        bool eligible=false;
        switch(node.rejectReason) {
            case Mfd::MotionNodeRejectReason::NoDiscreteCandidate:
            case Mfd::MotionNodeRejectReason::AmbiguousMatch:
            case Mfd::MotionNodeRejectReason::SubpixelFailure:
            case Mfd::MotionNodeRejectReason::UnobservableUnsafe:
                eligible=true;break;
            case Mfd::MotionNodeRejectReason::ForwardBackwardUnavailable:
                eligible=globalAccepted;break;
            default: break;
        }
        if(!eligible) continue;
        const auto mapped=grid.globalWarp.Map(node.centerRaw);
        if(!std::isfinite(mapped.x)||!std::isfinite(mapped.y)||mapped.x<2||mapped.y<2||
           mapped.x+2>=grid.sourceRawExtent.width||mapped.y+2>=grid.sourceRawExtent.height) continue;
        // Texture-free patches cannot determine an independent displacement.
        // Retain the already estimated global initializer with deliberately
        // broad covariance and low confidence; the photometric reliability
        // pass still rejects contradictory evidence at scene boundaries.
        node.state=Mfd::MotionNodeState::FlatSafe;
        node.residualRaw={};
        node.covarianceRaw={sigma*sigma,0,sigma*sigma};
        node.positionalSigmaRaw=sigma;
        node.confidence=.20;
        node.validFraction=1;
        ++converted;
    }
    grid.structuredCount=grid.flatSafeCount=grid.rejectedCount=0;
    for(const auto& node:grid.nodes) {
        if(node.state==Mfd::MotionNodeState::Structured) ++grid.structuredCount;
        else if(node.state==Mfd::MotionNodeState::FlatSafe) ++grid.flatSafeCount;
        else ++grid.rejectedCount;
    }
    grid.valid=grid.structuredCount+grid.flatSafeCount>0;
    return converted;
}

double Variance(const PreparedSource& source,Mfd::CfaSite site,
    double normalized,double gain) {
    const auto& profile=source.noise.sites[SiteIndex(site)];
    const double comparison=normalized*gain;
    Mfd::GainPropagatedVariance propagated;
    Mfd::EffectiveVariance effective;
    if(Mfd::PropagateKnownGainVariance(profile,gain,
        Mfd::NonnegativeShotPilot(comparison),
        source.frame.calibration.usableSpanByCfaSite[SiteIndex(site)],propagated)&&
       Mfd::ComposeEffectiveVariance(propagated.variance,0,
        Mfd::ResidualModelVariance(profile,comparison),0,propagated.darkVariance,
        propagated.effectiveDnStep*propagated.effectiveDnStep,1e-12,effective))
        return effective.gateVariance;
    return std::max(1e-12,(profile.shotScale*std::max(0.0,normalized)+
        profile.offsetVariance+profile.quantizationVariance)*gain*gain);
}

bool BuildPyramid(const PreparedSource& source,const std::filesystem::path& directory,
    const Mfd::Parameters& parameters,const std::function<bool()>& canceled,
    Mfd::CfaPlanePyramid& pyramid,std::string& error) {
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout)) {
        error="The local-alignment CFA layout is unsupported.";return false;
    }
    std::array<Mfd::CfaPyramidBasePlane,4> base;
    try {
        for(const auto site:kSites) {
            auto& plane=base[SiteIndex(site)];plane.site=site;
            plane.extent=layout.PlaneExtent(site,source.frame.activeExtent);
            const auto count=static_cast<std::size_t>(plane.extent.width*plane.extent.height);
            plane.signal.assign(count,0);plane.variance.assign(count,0);plane.validMask.assign(count,0);
        }
    } catch(const std::bad_alloc&) {error="The local-alignment pyramid base could not be allocated.";return false;}
    Mfd::DirectoryNormalizedTileCache cache(directory);
    for(std::uint32_t ty=0;ty<source.frame.tileRows;++ty) for(std::uint32_t tx=0;tx<source.frame.tileColumns;++tx) {
        if(canceled&&canceled()) {error="Canceled";return false;}
        Mfd::PreparedRawTile tile;
        if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit) return false;
        for(std::uint32_t y=0;y<tile.extent.height;++y) for(std::uint32_t x=0;x<tile.extent.width;++x) {
            const auto rawX=tile.originX+x,rawY=tile.originY+y;
            const auto packed=static_cast<std::size_t>(y)*tile.extent.width+x;
            const auto site=layout.SiteAt(rawX,rawY);auto& plane=base[SiteIndex(site)];
            const auto coordinate=layout.RawToPlane({static_cast<double>(rawX),static_cast<double>(rawY)},site);
            const auto index=static_cast<std::size_t>(coordinate.y)*plane.extent.width+static_cast<std::size_t>(coordinate.x);
            const double normalized=tile.normalizedMosaic[packed],gain=tile.comparisonGain[packed];
            const bool valid=!HardInvalid(tile.sampleFlags[packed])&&std::isfinite(normalized)&&std::isfinite(gain)&&gain>0;
            plane.validMask[index]=valid?1u:0u;
            if(valid) {plane.signal[index]=normalized*gain;plane.variance[index]=Variance(source,site,normalized,gain);}
        }
    }
    return Mfd::BuildCfaPlanePyramid(layout,source.frame.activeExtent,base,
        parameters.registration,pyramid,&error);
}

Mfd::SymmetricRawCovariance RegistrationFloor(const Mfd::Parameters& parameters) {
    const double floor=std::max(parameters.registration.warpCovarianceFloorRawPixels,
        parameters.registration.warpCovarianceEigenvalueMinRawPixels);
    return {floor*floor,0,floor*floor};
}

bool BuildFixedMotion(const PreparedSource& source,const Mfd::Parameters& parameters,
    Mfd::LocalMotionGrid& grid,std::string& error) {
    const auto extent=source.frame.activeExtent;
    if(extent.width<2||extent.height<2) {error="The bracket is too small for an alignment field.";return false;}
    grid={};grid.valid=true;grid.referenceRawExtent=extent;grid.sourceRawExtent=extent;
    grid.globalWarp=source.alignment;grid.width=grid.height=2;
    grid.spacingRawX=static_cast<double>(extent.width-1);grid.spacingRawY=static_cast<double>(extent.height-1);
    grid.structuredCount=4;grid.message="Accepted global warp with local reliability checks.";
    const auto covariance=RegistrationFloor(parameters);
    grid.nodes.resize(4);
    for(std::uint32_t y=0;y<2;++y) for(std::uint32_t x=0;x<2;++x) {
        auto& node=grid.nodes[y*2+x];node.gridX=x;node.gridY=y;
        node.centerRaw={x*grid.spacingRawX,y*grid.spacingRawY};node.covarianceRaw=covariance;
        node.state=Mfd::MotionNodeState::Structured;node.confidence=1;node.validFraction=1;
        node.positionalSigmaRaw=std::sqrt(std::max(covariance.xxRawPixelsSquared,covariance.yyRawPixelsSquared));
    }
    return true;
}

bool BuildReliability(const ProcessingRequest& request,const PreparedDataset& data,
    const PreparedSource& reference,PreparedSource& source,const Mfd::Parameters& parameters,
    const Mfd::CfaPlanePyramid& referencePyramid,
    const Mfd::CfaPlanePyramid& sourcePyramid,
    const Mfd::LocalMotionGrid& motion,const std::function<void(double)>& reportProgress,
    Mfd::ReliabilityMap& reliability,std::string& error) {
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(reference.frame.activeCfaPattern,layout)) {
        error="The local reliability CFA layout is unsupported.";return false;
    }
    Mfd::ReliabilityBuildRequest build;
    build.rawExtent=reference.frame.activeExtent;build.layout=layout;build.motionGrid=&motion;
    // Four raw pixels between confidence samples is still far finer than the
    // local-motion grid and ordinary scene edges, while reducing a 14 MP
    // capture from roughly 3.5 million statistical cells to 0.9 million.
    // Small validation images retain the original one-Bayer-cell grid.
    const auto sensorPixels=static_cast<std::uint64_t>(reference.frame.activeExtent.width)*
        reference.frame.activeExtent.height;
    build.cellStrideBayerCells=sensorPixels>=2'000'000?2u:1u;
    build.motionOptions.registration=parameters.registration;
    build.noiseQuality=static_cast<Mfd::NoiseModelQuality>(std::max(
        static_cast<std::uint8_t>(reference.noise.quality),static_cast<std::uint8_t>(source.noise.quality)));
    build.parameters=parameters;build.workerCount=ResolveBracketingWorkerCount(
        request.memoryBudgetBytes,request.workerCount);build.shouldCancel=request.shouldCancel;
    build.reportProgress=reportProgress;
    if(request.reportPresentation)build.reportObservation=[&](const Mfd::ReliabilityMap& map,bool provisional) {
        ObserveReliability(request,source,map,provisional);
    };
    build.residualEvaluator=[&](Mfd::RawCoordinate raw,Mfd::CfaSite site,
        const Mfd::LocalMotionFieldSample& local,Mfd::ReliabilityResidualSample& sample) {
        sample={};sample.valid=true;
        const auto* referenceLevel=Mfd::FindCfaPyramidLevel(referencePyramid,site,0);
        const auto* sourceLevel=Mfd::FindCfaPyramidLevel(sourcePyramid,site,0);
        if(!referenceLevel||!sourceLevel||raw.x<0||raw.y<0) return false;

        const auto referencePlane=layout.RawToPlane(raw,site);
        const auto referenceX=static_cast<std::int64_t>(std::llround(referencePlane.x));
        const auto referenceY=static_cast<std::int64_t>(std::llround(referencePlane.y));
        if(referenceX<0||referenceY<0||
           referenceX>=static_cast<std::int64_t>(referenceLevel->extent.width)||
           referenceY>=static_cast<std::int64_t>(referenceLevel->extent.height)) return true;
        const auto referenceIndex=static_cast<std::size_t>(referenceY)*referenceLevel->extent.width+
            static_cast<std::size_t>(referenceX);
        if(!referenceLevel->validMask[referenceIndex]) return true;
        const double referenceValue=referenceLevel->signal[referenceIndex];
        const double referenceVariance=referenceLevel->variance[referenceIndex];
        if(!std::isfinite(referenceValue)||!std::isfinite(referenceVariance)||referenceVariance<0) return true;

        const auto sourcePlane=layout.RawToPlane(local.sourceRaw,site);
        Mfd::KeysBicubicFootprint footprint;
        if(!BuildSupportedFootprint(sourcePlane,sourceLevel->extent,footprint)) return true;
        double sourceValue=0,sourceVariance=0,gradientPlaneX=0,gradientPlaneY=0;
        double gradientNoiseXX=0,gradientNoiseXY=0,gradientNoiseYY=0;
        for(std::size_t tap=0;tap<Mfd::kSameCfaTapCount;++tap) {
            const auto& coordinate=footprint.taps[tap];
            const auto index=static_cast<std::size_t>(coordinate.y)*sourceLevel->extent.width+
                static_cast<std::size_t>(coordinate.x);
            if(!sourceLevel->validMask[index]||!std::isfinite(sourceLevel->signal[index])||
               !std::isfinite(sourceLevel->variance[index])||sourceLevel->variance[index]<0) return true;
            const double coefficient=footprint.coefficients[tap];
            sourceValue+=coefficient*sourceLevel->signal[index];
            sourceVariance+=coefficient*coefficient*sourceLevel->variance[index];
            gradientPlaneX+=footprint.derivativeXPlane[tap]*sourceLevel->signal[index];
            gradientPlaneY+=footprint.derivativeYPlane[tap]*sourceLevel->signal[index];
            gradientNoiseXX+=footprint.derivativeXPlane[tap]*footprint.derivativeXPlane[tap]*sourceLevel->variance[index];
            gradientNoiseXY+=footprint.derivativeXPlane[tap]*footprint.derivativeYPlane[tap]*sourceLevel->variance[index];
            gradientNoiseYY+=footprint.derivativeYPlane[tap]*footprint.derivativeYPlane[tap]*sourceLevel->variance[index];
        }
        const double scale=source.scale;
        const double rawPixelsPerPlanePixel=sourceLevel->rawPixelsPerLevelPixel;
        if(!std::isfinite(scale)||scale<=0||!std::isfinite(rawPixelsPerPlanePixel)||
           rawPixelsPerPlanePixel<=0) return true;
        const double gradientRawX=scale*gradientPlaneX/rawPixelsPerPlanePixel;
        const double gradientRawY=scale*gradientPlaneY/rawPixelsPerPlanePixel;
        const double gradientNoiseScale=scale*scale/(rawPixelsPerPlanePixel*rawPixelsPerPlanePixel);
        const double registrationNoise=gradientNoiseScale*(
            gradientNoiseXX*local.covarianceRaw.xxRawPixelsSquared+
            2*gradientNoiseXY*local.covarianceRaw.xyRawPixelsSquared+
            gradientNoiseYY*local.covarianceRaw.yyRawPixelsSquared);
        const double registrationVariance=std::max(0.0,
            gradientRawX*gradientRawX*local.covarianceRaw.xxRawPixelsSquared+
            2*gradientRawX*gradientRawY*local.covarianceRaw.xyRawPixelsSquared+
            gradientRawY*gradientRawY*local.covarianceRaw.yyRawPixelsSquared-registrationNoise);
        const double exposureVariance=sourceValue*sourceValue*source.scaleVariance;
        sourceValue*=scale;sourceVariance*=scale*scale;
        const double alternateVariance=sourceVariance+registrationVariance+exposureVariance+
            parameters.fusion.numericalVarianceFloor;
        if(!std::isfinite(sourceValue)||!std::isfinite(alternateVariance)||alternateVariance<=0)
            return true;
        sample.hardValid=true;sample.referenceValue=referenceValue;
        sample.alternateValue=sourceValue;sample.referenceGateVariance=referenceVariance;
        sample.alternateGateVariance=alternateVariance;return true;
    };
    return BuildColorReliability(build,reliability,error);
}

std::shared_ptr<AlignmentInspection> MakeInspection(const Mfd::ReliabilityMap& map) {
    auto inspection=std::make_shared<AlignmentInspection>();
    inspection->width=static_cast<unsigned>(map.cellExtent.width);
    inspection->height=static_cast<unsigned>(map.cellExtent.height);
    inspection->rawWidth=static_cast<unsigned>(map.rawExtent.width);
    inspection->rawHeight=static_cast<unsigned>(map.rawExtent.height);
    inspection->confidence.resize(map.cells.size());inspection->rejectionBits.resize(map.cells.size());
    for(std::size_t i=0;i<map.cells.size();++i) {
        inspection->confidence[i]=static_cast<float>(map.cells[i].reliability);
        inspection->rejectionBits[i]=map.cells[i].rejectionBits;
    }
    return inspection;
}

} // namespace

bool BuildCapturePyramid(const PreparedSource& source,const std::filesystem::path& directory,
    const Mfd::Parameters& parameters,const std::function<bool()>& canceled,
    Mfd::CfaPlanePyramid& pyramid,std::string& error) {
    return BuildPyramid(source,directory,parameters,canceled,pyramid,error);
}

bool AlignLocal(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    if(request.recipe.alignmentMode!=AlignmentMode::AutomaticLocal) return true;
    if(data.sources.empty()||data.origin>=data.sources.size()) {error="Local alignment has no fixed reference.";return false;}
    Mfd::Parameters parameters;
    // Automatic local v2 adapts motion-node density to the sensor size. The
    // field remains dense enough for moderate local optical displacement,
    // while full-resolution reliability continues to reject inconsistent
    // measurements between nodes. The denoise processor keeps its denser
    // profile for use cases that explicitly request it.
    const auto sensorPixels=static_cast<std::uint64_t>(data.width)*data.height;
    parameters.registration.finestStridePlanePixels=
        std::min(data.width,data.height)<512?16:sensorPixels>=8'000'000?64:32;
    // Noise calibration quality remains visible in analysis and affects the
    // propagated variance. It must not make an otherwise sound spatial match
    // mathematically incapable of passing the alignment-validity threshold.
    parameters.reliability.noiseModelConfidence={1.0,1.0,1.0,1.0,0.0};
    // Automatic local v2 must remain useful in low-light and defocused areas.
    // Their standardized residuals are less certain than crisp daylight
    // texture, especially with a generic camera noise model. Preserve a wider
    // transition before declaring a patch contradictory; the merge still
    // downweights accepted uncertainty continuously and hard-rejects samples
    // beyond this safety gate.
    parameters.reliability.patchFullWeightSigma=2.0;
    parameters.reliability.patchZeroWeightSigma=6.0;
    parameters.reliability.reliabilityRemapMin=.05;
    parameters.reliability.reliabilityRemapMax=.75;
    const auto workers=ResolveBracketingWorkerCount(
        request.memoryBudgetBytes,request.workerCount);
    auto& reference=data.sources[data.origin];
    auto originInspection=std::make_shared<AlignmentInspection>();
    originInspection->width=data.width/2;originInspection->height=data.height/2;
    originInspection->rawWidth=data.width;originInspection->rawHeight=data.height;
    originInspection->confidence.assign(static_cast<std::size_t>(originInspection->width)*originInspection->height,1.f);
    originInspection->rejectionBits.assign(originInspection->confidence.size(),0);
    reference.alignmentInspection=originInspection;reference.alignmentDiagnostic.localApplied=true;
    reference.alignmentDiagnostic.backend="fixed-reference";

    ReportPresentation(request,ProcessingStage::LocalAlignment,0,0,reference.id,"Reference pyramid");
    Mfd::CfaPlanePyramid referencePyramid;
    GpuMotionSession gpuSession(request);
    if(request.reportProgress) request.reportProgress(.10,"Building reference alignment pyramid");
    {
        ProcessingTimer timer(data.stageSeconds,"local_pyramids");
        if(!BuildPyramid(reference,data.directory,parameters,request.shouldCancel,referencePyramid,error)) return false;
    }
    const auto alternateCount=std::count_if(data.sources.begin(),data.sources.end(),
        [&](const PreparedSource& source){return source.enabled&&source.id!=reference.id;});
    std::size_t alternateOrdinal=0;
    for(std::size_t index=0;index<data.sources.size();++index) {
        if(index==data.origin||!data.sources[index].enabled) continue;
        auto& source=data.sources[index];
        const double captureStart=.12+.40*alternateOrdinal/std::max<std::size_t>(1,alternateCount);
        const double captureSpan=.40/std::max<std::size_t>(1,alternateCount);
        ++alternateOrdinal;
        ReportPresentation(request,ProcessingStage::LocalAlignment,alternateOrdinal-1,alternateCount,source.id);
        if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
        if(request.reportProgress) request.reportProgress(captureStart,"Building alternate alignment pyramid");
        Mfd::CfaPlanePyramid alternatePyramid;
        {
            ProcessingTimer timer(data.stageSeconds,"local_pyramids");
            if(!BuildPyramid(source,data.directory,parameters,request.shouldCancel,alternatePyramid,error)) return false;
        }
        Mfd::BidirectionalLocalMotionRequest motionRequest;
        motionRequest.reference=&referencePyramid;motionRequest.alternate=&alternatePyramid;
        motionRequest.referenceToAlternate=source.alignment;motionRequest.exposureScale=source.scale;
        const auto floor=RegistrationFloor(parameters);
        motionRequest.forwardGlobalCovariance=[floor](Mfd::RawCoordinate,Mfd::SymmetricRawCovariance& value){value=floor;return true;};
        motionRequest.reverseGlobalCovariance=motionRequest.forwardGlobalCovariance;
        motionRequest.options.registration=parameters.registration;motionRequest.workerCount=workers;
        motionRequest.shouldCancel=request.shouldCancel;
        if(request.reportPresentation)motionRequest.reportObservation=[&](const Mfd::LocalMotionGrid& grid,bool provisional) {
            ObserveMotion(request,source,grid,provisional);
        };
        motionRequest.reportProgress=[&request,captureStart,captureSpan](double fraction) {
            if(request.reportProgress) request.reportProgress(
                captureStart+captureSpan*(.12+.68*std::clamp(fraction,0.0,1.0)),
                "Searching local alignment");
        };
        Mfd::BidirectionalLocalMotionResult motion;const bool reuseMotion=bool(source.localMotion);
        bool accepted=reuseMotion;std::string localError;
        if(reuseMotion) motion.forward=*source.localMotion;
        // A high logical-core count does not imply that the dependency-heavy
        // bidirectional wavefront search keeps every core busy. Large RAWs
        // benefit from GPU candidate scoring even on a wide CPU, while tiny
        // images avoid the context-dispatch and upload overhead.
        const bool useGpu=request.preferGpuRegistration&&request.executeOpenGlTask&&
            (sensorPixels>=2'000'000||workers<8);
        if(useGpu&&!accepted) {
            ProcessingTimer timer(data.stageSeconds,"local_search_gpu_and_cpu_verification");
            if(gpuSession.BeginPair(referencePyramid,alternatePyramid,localError)) {
                auto accelerated=motionRequest;
                accelerated.evaluateDiscreteCandidates=[&](const Mfd::LocalMotionDirectionRequest& direction,
                    const std::vector<Mfd::LocalMotionDiscreteCandidate>& candidates,
                    std::vector<Mfd::LocalMotionDiscreteScore>& scores,std::string& evaluationError) {
                    return gpuSession.Evaluate(direction,candidates,scores,evaluationError);
                };
                accepted=Mfd::EstimateBidirectionalLocalMotion(accelerated,motion,&localError);
            }
            if(accepted) source.alignmentDiagnostic.backend="gpu-search/cpu-verified";
        }
        if(!accepted) {
            ProcessingTimer timer(data.stageSeconds,"local_search_cpu");
            if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
            localError.clear();accepted=Mfd::EstimateBidirectionalLocalMotion(motionRequest,motion,&localError);
            source.alignmentDiagnostic.backend="cpu-reference";
        }
        bool globalFallback=false;
        if(!accepted) {
            globalFallback=true;motion={};
            if(!BuildFixedMotion(source,parameters,motion.forward,error)) return false;
            diagnostics.push_back(source.id+": local motion was ambiguous; the global warp is guarded by local reliability checks ("+localError+").");
        }
        const auto globalFallbackNodes=reuseMotion?0:AddConservativeGlobalFallbackNodes(
            motion.forward,source.alignmentDiagnostic.accepted,parameters);
        if(!reuseMotion) ExtendMotionBoundary(motion.forward);
        ObserveMotion(request,source,motion.forward,false);
        if(globalFallbackNodes) diagnostics.push_back(source.id+": "+
            std::to_string(globalFallbackNodes)+
            " texture-free motion nodes use the global initializer with reduced confidence.");
        Mfd::ReliabilityMap reliability;
        Mfd::LocalMotionOptions refinementOptions;refinementOptions.registration=parameters.registration;
        if(!reuseMotion) RefineLocallyRegisteredExposure(request,reference,source,referencePyramid,alternatePyramid,
            motion.forward,refinementOptions,diagnostics);
        if(request.reportProgress) request.reportProgress(captureStart+captureSpan*.80,
            "Evaluating alignment confidence");
        const auto reliabilityProgress=[&request,captureStart,captureSpan](double fraction) {
            if(request.reportProgress) request.reportProgress(
                captureStart+captureSpan*(.80+.20*std::clamp(fraction,0.0,1.0)),
                "Evaluating alignment confidence");
        };
        const auto reliabilityStart=std::chrono::steady_clock::now();
        if(!BuildReliability(request,data,reference,source,parameters,
            referencePyramid,alternatePyramid,motion.forward,
            reliabilityProgress,reliability,localError)) {
            source.alignmentInspection=std::make_shared<AlignmentInspection>();
            source.alignmentInspection->width=data.width/2;source.alignmentInspection->height=data.height/2;
            source.alignmentInspection->rawWidth=data.width;source.alignmentInspection->rawHeight=data.height;
            source.alignmentInspection->confidence.assign(static_cast<std::size_t>(data.width/2)*(data.height/2),0.f);
            source.alignmentInspection->rejectionBits.assign(source.alignmentInspection->confidence.size(),
                Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::AlignmentInvalid));
            source.alignmentDiagnostic.localApplied=true;source.alignmentDiagnostic.acceptedCoverage=0;
            source.alignmentDiagnostic.uncertainCoverage=0;
            source.alignmentDiagnostic.rejectedCoverage=1;
            source.alignmentDiagnostic.message="Local alignment was not reliable; this capture is excluded where the reference is usable.";
            diagnostics.push_back(source.id+": "+source.alignmentDiagnostic.message+" "+localError);
            continue;
        }
        auto store=std::make_shared<Mfd::ReliabilityStore>();
        data.stageSeconds["local_reliability"]+=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-reliabilityStart).count();
        if(!Mfd::BuildReliabilityStore(reliability,Mfd::ReliabilityStorageFormat::Uint16Unorm,
            parameters.reliability.storageTileCells,*store,&error)) return false;
        source.localMotion=std::make_shared<Mfd::LocalMotionGrid>(std::move(motion.forward));
        source.reliability=std::move(store);source.alignmentInspection=MakeInspection(reliability);
        std::size_t acceptedCells=0,uncertainCells=0,rejectedCells=0;
        for(const auto& cell:reliability.cells) {
            if(HardReliabilityRejection(cell.rejectionBits)) ++rejectedCells;
            else if(cell.reliability>=parameters.reliability.frameUsableReliabilityThreshold) ++acceptedCells;
            else ++uncertainCells;
        }
        source.alignmentDiagnostic.localApplied=true;
        const double cellCount=static_cast<double>(reliability.cells.size());
        source.alignmentDiagnostic.acceptedCoverage=cellCount?acceptedCells/cellCount:0;
        source.alignmentDiagnostic.uncertainCoverage=cellCount?uncertainCells/cellCount:0;
        source.alignmentDiagnostic.rejectedCoverage=cellCount?rejectedCells/cellCount:1;
        const bool weakCapture=source.alignmentDiagnostic.acceptedCoverage+
            source.alignmentDiagnostic.uncertainCoverage<.25;
        source.alignmentDiagnostic.accepted=!weakCapture;
        source.alignmentDiagnostic.message=weakCapture
            ?"Automatic local alignment was weak; unreliable regions were excluded."
            :source.alignmentDiagnostic.uncertainCoverage>.25
                ?"Automatic local alignment accepted with reduced weight in uncertain regions."
            :globalFallback
                ?"Global alignment retained with local reliability protection."
                :"Automatic local alignment accepted.";
        ReportPresentation(request,ProcessingStage::LocalAlignment,alternateOrdinal,alternateCount,source.id,
            "Accepted coverage "+std::to_string(int(source.alignmentDiagnostic.acceptedCoverage*100))+"%");
        diagnostics.push_back(source.id+": "+source.alignmentDiagnostic.message+" accepted="+
            std::to_string(source.alignmentDiagnostic.acceptedCoverage*100)+"% uncertain="+
            std::to_string(source.alignmentDiagnostic.uncertainCoverage*100)+"% rejected="+
            std::to_string(source.alignmentDiagnostic.rejectedCoverage*100)+"% backend="+
            source.alignmentDiagnostic.backend);
    }
    const auto gpu=gpuSession.Diagnostics();
    data.stageSeconds["gpu_dispatch_upload_readback"]+=gpu.evaluationSeconds;
    if(gpu.dispatchCount) diagnostics.push_back("Local GPU batches="+std::to_string(gpu.dispatchCount)+
        ", scored candidates="+std::to_string(gpu.scoredCandidateCount)+
        ", uploaded CFA layers="+std::to_string(gpu.uploadedLayers)+
        ". CPU verification runs outside the render owner.");
    return true;
}

} // namespace Raw::Bracketing
