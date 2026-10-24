#include "ProcessingInternal.h"
#include "BurstAlignment.h"
#include "PresentationEvidence.h"
#include "BorderSupport.h"
#include "CaptureReliability.h"
#include "ExposureRefinement.h"
#include "AlignmentProposalInput.h"
#include "GroupAlignment.h"
#include "AlignmentExposureCohorts.h"
#include "AlignmentAnchorEvidence.h"

#include "Raw/MultiFrameDenoise/GpuLocalMotion.h"
#include "GpuMotionSession.h"
#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

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

std::size_t AddConservativeGlobalFallbackNodes(
    Mfd::LocalMotionGrid& grid,bool globalAccepted,const Mfd::Parameters& parameters) {
    // A failed phase/affine solve leaves identity as a search seed, not an
    // observed camera transform. Photometric agreement in a dark, noisy patch
    // cannot turn that seed into geometric evidence.
    if(!globalAccepted) return 0;
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
    const std::array<Mfd::CfaSite,4> sitesByOffset={
        layout.SiteAt(0,0),layout.SiteAt(1,0),layout.SiteAt(0,1),layout.SiteAt(1,1)};
    try {
        for(const auto site:kSites) {
            auto& plane=base[SiteIndex(site)];plane.site=site;
            plane.extent=layout.PlaneExtent(site,source.frame.activeExtent);
            const auto count=static_cast<std::size_t>(plane.extent.width*plane.extent.height);
            plane.signal.assign(count,0);plane.variance.assign(count,0);plane.validMask.assign(count,0);
            plane.clippedMask.assign(count,0);
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
            const auto site=sitesByOffset[(rawY%2)*2+rawX%2];auto& plane=base[SiteIndex(site)];
            // This pixel belongs to its own Bayer parity. RawToPlane therefore
            // has exact integer coordinates, independent of the CFA pattern.
            const auto index=static_cast<std::size_t>(rawY/2)*plane.extent.width+rawX/2;
            const double normalized=tile.normalizedMosaic[packed],gain=tile.comparisonGain[packed];
            const bool valid=!HardInvalid(tile.sampleFlags[packed])&&std::isfinite(normalized)&&std::isfinite(gain)&&gain>0;
            const auto flags=tile.sampleFlags[packed];
            const bool clipped=std::isfinite(normalized)&&std::isfinite(gain)&&gain>0&&
                !Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective)&&
                !Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::DecoderRepaired)&&
                (Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||
                 Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip));
            plane.validMask[index]=valid?1u:0u;
            plane.clippedMask[index]=clipped?1u:0u;
            if(valid||clipped) {plane.signal[index]=normalized*gain;plane.variance[index]=Variance(source,site,normalized,gain);}
        }
    }
    return Mfd::BuildCfaPlanePyramid(layout,source.frame.activeExtent,std::move(base),
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
    const auto parameters=CaptureAlignmentParameters(data);
    const auto workers=ResolveBracketingWorkerCount(
        request.memoryBudgetBytes,request.workerCount);
    auto& reference=data.sources[data.origin];
    // A confidence refresh reuses every accepted motion field and samples
    // only level zero. Do not rebuild unused coarse search levels. Keep the
    // original registration parameters for motion/reliability evaluation.
    const bool reliabilityOnly=std::all_of(data.sources.begin(),data.sources.end(),
        [&](const PreparedSource& source){return !source.enabled||&source==&reference||source.localMotion;});
    if(!reliabilityOnly&&(!request.preferGpuRegistration||!request.executeOpenGlTask)) {
        error="GPU local search is required but disabled or unavailable.";
        return false;
    }
    auto pyramidParameters=parameters;
    if(reliabilityOnly)pyramidParameters.registration.pyramidLevels=1;
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
        if(!BuildPyramid(reference,data.directory,pyramidParameters,request.shouldCancel,referencePyramid,error)) return false;
    }
    const auto alternateCount=std::count_if(data.sources.begin(),data.sources.end(),
        [&](const PreparedSource& source){return source.enabled&&source.id!=reference.id;});
    std::size_t alternateOrdinal=0;
    std::vector<AlignmentExposureCohortInput> cohortInputs;
    cohortInputs.reserve(data.sources.size());
    for(const auto& source:data.sources)
        cohortInputs.push_back({source.enabled,source.exposureCalibrated,source.scale});
    auto cohorts=BuildAlignmentExposureCohorts(cohortInputs,data.origin);
    {
        ProcessingTimer timer(data.stageSeconds,"alignment_anchor_ranking");
        if(!RankAlignmentAnchors(request,data,cohorts,diagnostics,error))return false;
    }
    // RefreshCaptureReliability replaces the displayed accepted status with
    // local coverage. Keep the original global evidence for pair fallbacks.
    for(auto& source:data.sources)if(!source.localMotion)
        source.globalAlignmentAccepted=source.alignmentDiagnostic.accepted;
    for(const auto index:cohorts.order) {
        const auto anchor=cohorts.anchorForSource[index];
        // A brighter cohort supplies shadow structure absent from the origin.
        // Keep shorter exposures independent, and retain existing composed
        // motion on reliability refresh even if refined gains change cohorts.
        const bool useGroup=cohorts.memberCount[index]>1&&anchor!=data.origin&&
            data.sources[anchor].scale<reference.scale&&!request.recipe.aiAlignment&&
            request.alignmentProposals.empty();
        if(!data.sources[index].localMotion)data.sources[index].exposureGroupMotion=useGroup;
    }
    std::size_t pyramidReference=data.origin;
    std::size_t gpuPyramidReference=data.origin;
    for(const auto index:cohorts.order) {
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
            if(!BuildPyramid(source,data.directory,pyramidParameters,request.shouldCancel,alternatePyramid,error)) return false;
        }
        Mfd::BidirectionalLocalMotionRequest motionRequest;
        const auto anchorIndex=cohorts.anchorForSource[index];
        const bool reuseGroup=source.localMotion!=nullptr;
        bool groupPair=!reuseGroup&&source.exposureGroupMotion&&
            anchorIndex<data.sources.size()&&anchorIndex!=index&&anchorIndex!=data.origin&&data.sources[anchorIndex].localMotion&&
            data.sources[anchorIndex].localMotionHasRegisteredGeometry&&
            data.sources[anchorIndex].localMotion->structuredCount+data.sources[anchorIndex].localMotion->flatSafeCount>0;
        Mfd::AffineModel pairWarp;
        if(groupPair&&!GroupRelativeWarp(data.sources[anchorIndex],source,pairWarp))groupPair=false;
        const auto pairReference=groupPair?anchorIndex:data.origin;
        if(pyramidReference!=pairReference) {
            ProcessingTimer timer(data.stageSeconds,"local_pyramids");
            referencePyramid={};
            if(!BuildPyramid(data.sources[pairReference],data.directory,pyramidParameters,request.shouldCancel,referencePyramid,error))return false;
            pyramidReference=pairReference;
        }
        motionRequest.reference=&referencePyramid;motionRequest.alternate=&alternatePyramid;
        motionRequest.referenceToAlternate=groupPair?pairWarp:source.alignment;
        motionRequest.exposureScale=source.scale/(groupPair?data.sources[anchorIndex].scale:1.);
        const auto floor=RegistrationFloor(parameters);
        motionRequest.forwardGlobalCovariance=[floor](Mfd::RawCoordinate,Mfd::SymmetricRawCovariance& value){value=floor;return true;};
        motionRequest.reverseGlobalCovariance=motionRequest.forwardGlobalCovariance;
        motionRequest.options.registration=parameters.registration;motionRequest.workerCount=workers;
        motionRequest.shouldCancel=request.shouldCancel;
        {
            ProcessingTimer timer(data.stageSeconds,"alignment_proposal_load");
            if(!source.localMotion&&!AttachAlignmentProposal(request,source.id,motionRequest,error))return false;
        }
        if(request.reportPresentation&&!groupPair)motionRequest.reportObservation=[&](const Mfd::LocalMotionGrid& grid,bool provisional) {
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
        if(!reuseMotion) {
            // CPU orchestration and verification remain part of this hybrid
            // algorithm, but discrete search must never silently switch backend.
            if(!request.preferGpuRegistration||!request.executeOpenGlTask) {
                error=source.id+": GPU local search is required but disabled or unavailable.";
                return false;
            }
            ProcessingTimer timer(data.stageSeconds,"local_search_gpu_and_cpu_verification");
            if(!gpuSession.BeginPair(referencePyramid,alternatePyramid,localError,
                gpuPyramidReference!=pyramidReference)) {
                error=source.id+": GPU local search could not initialize: "+localError;
                return false;
            }
            gpuPyramidReference=pyramidReference;
            auto accelerated=motionRequest;
            accelerated.evaluateDiscreteCandidates=[&](const Mfd::LocalMotionDirectionRequest& direction,
                const std::vector<Mfd::LocalMotionDiscreteCandidate>& candidates,
                std::vector<Mfd::LocalMotionDiscreteScore>& scores,std::string& evaluationError) {
                return gpuSession.Evaluate(direction,candidates,scores,evaluationError);
            };
            accepted=Mfd::EstimateBidirectionalLocalMotion(accelerated,motion,&localError);
            source.alignmentDiagnostic.backend="gpu-search/cpu-verified";
        }
        if(motionRequest.forwardProposal&&!reuseMotion) {
            source.alignmentDiagnostic.backend+="/external-proposal-raw-verified";
            std::array<unsigned,13> rejections{};
            for(const auto& node:motion.forward.nodes)if(node.state==Mfd::MotionNodeState::Rejected)
                ++rejections[static_cast<unsigned>(node.rejectReason)];
            std::string rejected;
            for(unsigned r=0;r<rejections.size();++r)if(rejections[r])
                rejected+=" "+std::string(Mfd::MotionNodeRejectReasonName(static_cast<Mfd::MotionNodeRejectReason>(r)))+"="+std::to_string(rejections[r]);
            diagnostics.push_back(source.id+": proposal-assisted RAW verification before global fallback: structured="+
                std::to_string(motion.forward.structuredCount)+", flat-safe="+std::to_string(motion.forward.flatSafeCount)+
                ", rejected="+std::to_string(motion.forward.rejectedCount)+rejected);
        }
        // Latch genuine registration evidence before fallback nodes, boundary
        // extension or composition can manufacture Structured node labels.
        // Reliability refresh reuses the field and its original provenance.
        if(!reuseMotion) {
            const bool rawStructured=accepted&&motion.forward.structuredCount>0;
            const bool pairGlobal=source.globalAlignmentAccepted&&
                (!groupPair||data.sources[anchorIndex].globalAlignmentAccepted);
            source.localMotionHasRegisteredGeometry=!accepted?source.globalAlignmentAccepted:
                groupPair?(data.sources[anchorIndex].localMotionHasRegisteredGeometry&&
                    (rawStructured||pairGlobal)):
                (source.globalAlignmentAccepted||rawStructured);
            if(accepted&&!source.localMotionHasRegisteredGeometry)
                diagnostics.push_back(source.id+": photometric-only local support has no independently established geometry; excluded from exposure calibration evidence.");
        }
        bool globalFallback=false;
        if(!accepted) {
            // Missing scene evidence may use the guarded global warp. Resource,
            // contract and execution failures must not silently reduce quality.
            if(motion.failure!=Mfd::LocalMotionFailure::NoUsableNodes) {
                error=source.id+": local alignment could not complete: "+localError;
                return false;
            }
            globalFallback=true;motion={};
            groupPair=false;
            if(!BuildFixedMotion(source,parameters,motion.forward,error)) return false;
            if(!source.globalAlignmentAccepted) {
                for(auto& node:motion.forward.nodes) {
                    node.state=Mfd::MotionNodeState::Rejected;node.confidence=0;
                }
                motion.forward.structuredCount=0;
                motion.forward.rejectedCount=motion.forward.nodes.size();
                diagnostics.push_back(source.id+": neither global nor local motion was established; capture correspondence remains rejected.");
            } else diagnostics.push_back(source.id+": local motion was ambiguous; the global warp is guarded by local reliability checks ("+localError+").");
        }
        const bool pairGlobalAccepted=source.globalAlignmentAccepted&&
            (!groupPair||data.sources[anchorIndex].globalAlignmentAccepted);
        if(!reuseMotion&&index!=anchorIndex&&!groupPair)source.exposureGroupMotion=false;
        const auto globalFallbackNodes=reuseMotion?0:AddConservativeGlobalFallbackNodes(
            motion.forward,pairGlobalAccepted,parameters);
        if(!reuseMotion) ExtendMotionBoundary(motion.forward);
        if(groupPair) {
            motion.forward=ComposeGroupMotion(data.sources[anchorIndex],source,motion.forward,motionRequest.options);
            diagnostics.push_back(source.id+": aligned within calibrated exposure cohort against "+data.sources[anchorIndex].id+
                "; composed with its verified mapping to the output reference.");
        }
        if(pyramidReference!=data.origin) {
            ProcessingTimer timer(data.stageSeconds,"local_pyramids");
            referencePyramid={};
            if(!BuildPyramid(reference,data.directory,pyramidParameters,request.shouldCancel,referencePyramid,error))return false;
            pyramidReference=data.origin;
        }
        ObserveMotion(request,source,motion.forward,false);
        if(globalFallbackNodes) diagnostics.push_back(source.id+": "+
            std::to_string(globalFallbackNodes)+
            " texture-free motion nodes use the global initializer with reduced confidence.");
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
        source.localMotion=std::make_shared<Mfd::LocalMotionGrid>(std::move(motion.forward));
        source.refinedMotion.reset();++source.motionRevision;
        if(!RefreshCaptureReliability(request,reference,source,parameters,
            referencePyramid,alternatePyramid,reliabilityProgress,localError)) {
            if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
            error=source.id+": alignment confidence could not be evaluated: "+localError;
            return false;
        }
        data.stageSeconds["local_reliability"]+=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-reliabilityStart).count();
        if(globalFallback&&source.alignmentDiagnostic.accepted&&source.alignmentDiagnostic.uncertainCoverage<=.25)
            source.alignmentDiagnostic.message="Global alignment retained with local reliability protection.";
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
