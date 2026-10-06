#include "Alignment.h"
#include "PresentationEvidence.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
namespace {

constexpr double kPi = 3.14159265358979323846;

double ProxyLuminance(const CapturePreview& preview, std::size_t index) {
    const auto offset=index*3;
    const double r=preview.rgb[offset],g=preview.rgb[offset+1],b=preview.rgb[offset+2];
    return .2126*r+.7152*g+.0722*b;
}
double ProxySiteValue(const CapturePreview& preview,std::size_t index,Mfd::CfaSite site) {
    const unsigned channel=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
    return preview.rgb[index*3+channel];
}

bool SampleProxySource(const PreparedSource& source,Mfd::RawCoordinate sourceRaw,
    double& value,Mfd::RawSignalGradient* gradient=nullptr) {
    if(!source.original||!source.original->width||!source.original->height||!source.proxyStride) return false;
    const auto& preview=*source.original;
    const double px=(sourceRaw.x-.5)/source.proxyStride;
    const double py=(sourceRaw.y-.5)/source.proxyStride;
    if(!std::isfinite(px)||!std::isfinite(py)||px<0||py<0||px>=preview.width-1||py>=preview.height-1) return false;
    const auto x0=static_cast<unsigned>(std::floor(px)),y0=static_cast<unsigned>(std::floor(py));
    const double fx=px-x0,fy=py-y0;
    const std::array<std::size_t,4> indices={
        static_cast<std::size_t>(y0)*preview.width+x0,
        static_cast<std::size_t>(y0)*preview.width+x0+1,
        static_cast<std::size_t>(y0+1)*preview.width+x0,
        static_cast<std::size_t>(y0+1)*preview.width+x0+1};
    const std::array<double,4> weights={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    std::array<double,4> samples{};
    for(std::size_t i=0;i<4;++i) {
        if(weights[i]>1e-12&&preview.clipped[indices[i]]) return false;
        samples[i]=ProxyLuminance(preview,indices[i]);
        if(!std::isfinite(samples[i])) return false;
    }
    value=0;for(std::size_t i=0;i<4;++i)value+=weights[i]*samples[i];
    if(gradient) {
        gradient->dxPerRawPixel=((1-fy)*(samples[1]-samples[0])+fy*(samples[3]-samples[2]))/source.proxyStride;
        gradient->dyPerRawPixel=((1-fx)*(samples[2]-samples[0])+fx*(samples[3]-samples[1]))/source.proxyStride;
    }
    return std::isfinite(value);
}

double PreliminaryScale(const ProcessingRequest& request,const PreparedDataset& data,const PreparedSource& source) {
    for(const auto& group:request.recipe.groups) for(const auto& frame:group.frames) if(frame.id==source.id&&frame.manualExposure)
        return std::exp2(-frame.relativeEv);
    const auto& origin=data.sources[data.origin].metadata;
    const auto exposure=[](const RawMetadata& metadata) {
        const double aperture=metadata.apertureFNumber>0?metadata.apertureFNumber:1.0;
        return metadata.exposureTimeSeconds>0&&metadata.isoSpeed>0
            ? static_cast<double>(metadata.exposureTimeSeconds)*metadata.isoSpeed/(aperture*aperture):0.0;
    };
    const double a=exposure(origin),b=exposure(source.metadata);
    return a>0&&b>0?a/b:1.0;
}

std::vector<Mfd::AffineReferenceSample> ReferenceSamples(const PreparedSource& source) {
    std::vector<Mfd::AffineReferenceSample> result;
    if(!source.original) return result;
    const auto& preview=*source.original;
    const unsigned step=std::max(1u,static_cast<unsigned>(std::ceil(std::sqrt(
        static_cast<double>(preview.width)*preview.height/16000.0))));
    result.reserve(static_cast<std::size_t>((preview.width+step-1)/step)*((preview.height+step-1)/step));
    for(unsigned y=1;y+1<preview.height;y+=step) for(unsigned x=1;x+1<preview.width;x+=step) {
        const auto index=static_cast<std::size_t>(y)*preview.width+x;
        if(preview.clipped[index]) continue;
        const double value=ProxyLuminance(preview,index);
        if(!std::isfinite(value)||value<=.002) continue;
        Mfd::AffineReferenceSample sample;
        sample.referenceRaw={x*static_cast<double>(source.proxyStride)+.5,y*static_cast<double>(source.proxyStride)+.5};
        sample.site=Mfd::CfaSite::Green0;sample.referenceValue=value;
        sample.referenceVariance=1e-5+1e-4*std::max(0.0,value);
        result.push_back(sample);
    }
    return result;
}

double RotationDegrees(const Mfd::AffineModel& model) {
    return std::atan2(model.linear[2]-model.linear[1],model.linear[0]+model.linear[3])*180.0/kPi;
}

double PlaneConsensusTolerance(const PreparedSource& source) {
    // Each alignment-proxy sample represents proxyStride sensor pixels. A
    // fixed four-pixel cross-plane cutoff becomes stricter as the overview is
    // decimated for a large sensor or a large burst. Permit half of one proxy
    // sample, within conservative absolute bounds; the combined peak and the
    // later local reliability pass still have to validate the result.
    return std::clamp(.5*static_cast<double>(source.proxyStride),6.0,12.0);
}

} // namespace

bool SampleAlignmentProxy(const PreparedSource& source,Mfd::RawCoordinate referenceRaw,double& value) {
    if(source.localMotion) {
        Mfd::LocalMotionFieldSample motion;Mfd::LocalMotionOptions options;
        if(!EvaluateCaptureMotion(source,referenceRaw,options,motion)||
            motion.alignmentConfidence<.2) return false;
        return SampleProxySource(source,motion.sourceRaw,value,nullptr);
    }
    if(source.alignmentDiagnostic.localApplied&&source.alignmentDiagnostic.backend!="fixed-reference") return false;
    return SampleProxySource(source,source.alignment.Map(referenceRaw),value,nullptr);
}

bool Align(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    if(data.sources.empty()||data.origin>=data.sources.size()) {error="Bracketing alignment has no reference capture.";return false;}
    auto& reference=data.sources[data.origin];
    const Mfd::RawCoordinate center={.5*(data.width-1.0),.5*(data.height-1.0)};
    for(auto& source:data.sources) {
        source.alignment={};source.alignment.centerRaw=center;
        source.alignmentDiagnostic={};source.alignmentDiagnostic.message="Fixed reference coordinates.";
        source.scale=PreliminaryScale(request,data,source);
    }
    reference.alignmentDiagnostic.message="Alignment reference (fixed output grid).";
    if(request.recipe.alignmentMode==AlignmentMode::FixedCoordinates) {
        diagnostics.push_back("Alignment is off. Captures use identical sensor coordinates.");
        return true;
    }
    if(!reference.original) {error="The alignment reference proxy is unavailable.";return false;}
    std::size_t alternate=0;
    const auto alternateCount=data.sources.size()-1;
    for(std::size_t i=0;i<data.sources.size();++i) {
        if(i==data.origin) continue;
        auto& source=data.sources[i];
        ++alternate;
        ReportPresentation(request,ProcessingStage::GlobalAlignment,alternate-1,alternateCount,source.id);
        if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
        if(!source.original||source.original->width!=reference.original->width||source.original->height!=reference.original->height||source.proxyStride!=reference.proxyStride) {
            error="Alignment proxies do not share a common sensor grid for "+source.id+".";return false;
        }
        const auto count=static_cast<std::size_t>(reference.original->width)*reference.original->height;
        if(count>request.memoryBudgetBytes/96) {error="Alignment proxies exceed the available bracketing memory budget.";return false;}
        Mfd::PhaseCorrelationOptions options;
        options.minimumValidFraction=.20;options.minimumTextureVariance=1e-9;
        options.minimumPeakToSidelobeRatio=5;options.minimumPeakUniqueness=.01;
        options.minimumOverlapFraction=.55;
        options.maximumPlaneDisagreementRawPixels=PlaneConsensusTolerance(source);
        // Phase correlation needs scene geometry, including highlight boundaries.
        // A shared clipping/dark mask stamps identical holes into both images
        // before they are aligned and can create a dominant zero-shift peak.
        // Match their exposure and ceiling instead. These bounded values are
        // only a motion initializer; radiometric sampling still rejects clipping.
        const double phaseCeiling=.8*std::min(1.0,source.scale);
        std::vector<Mfd::GlobalPlanePair> pairs;
        for(const auto site:{Mfd::CfaSite::Red,Mfd::CfaSite::Green0,Mfd::CfaSite::Green1,Mfd::CfaSite::Blue}) {
            Mfd::GlobalPlanePair sitePair;sitePair.site=site;
            sitePair.extent={reference.original->width,reference.original->height};
            sitePair.rawPixelsPerPlanePixel=reference.proxyStride;
            sitePair.reference.resize(count);sitePair.alternate.resize(count);sitePair.validMask.resize(count);
            for(std::size_t p=0;p<count;++p) {
                sitePair.reference[p]=ProxySiteValue(*reference.original,p,site);
                sitePair.alternate[p]=ProxySiteValue(*source.original,p,site)*source.scale;
                sitePair.validMask[p]=std::isfinite(sitePair.reference[p])&&
                    std::isfinite(sitePair.alternate[p])?1u:0u;
                sitePair.reference[p]=std::clamp(sitePair.reference[p],0.0,phaseCeiling);
                sitePair.alternate[p]=std::clamp(sitePair.alternate[p],0.0,phaseCeiling);
            }
            pairs.push_back(std::move(sitePair));
        }
        Mfd::PhaseTranslationResult phase;std::string phaseError;
        if(!Mfd::EstimateMultichannelPhaseTranslation(pairs,options,phase,&phaseError)||!phase.accepted) {
            std::string planeDetails;
            for(const auto& plane:phase.planes) if(plane.usable) planeDetails+=" "+std::string(Mfd::CfaSiteName(plane.site))+"=("+
                std::to_string(plane.translationRaw.x)+","+std::to_string(plane.translationRaw.y)+")";
            if(request.recipe.alignmentMode==AlignmentMode::AutomaticLocal) {
                source.alignment={};source.alignment.centerRaw=center;
                source.alignmentDiagnostic.accepted=false;
                source.alignmentDiagnostic.model=CaptureAlignment::Model::Identity;
                source.alignmentDiagnostic.message=
                    "Global alignment was uncertain; Automatic local will test the capture from the fixed-grid initializer.";
                diagnostics.push_back(source.id+": "+source.alignmentDiagnostic.message+
                    " "+(phase.message.empty()?phaseError:phase.message)+planeDetails);
                ObserveAlignment(request,data,source);
                ReportPresentation(request,ProcessingStage::GlobalAlignment,alternate,alternateCount,source.id);
                continue;
            }
            error="Automatic alignment failed for "+source.id+": "+(phase.message.empty()?phaseError:phase.message)+planeDetails+
                " Choose fixed coordinates only if the captures are already registered.";
            return false;
        }
        source.alignment.translationRaw=phase.translationRaw;
        source.alignmentDiagnostic.accepted=true;
        source.alignmentDiagnostic.model=CaptureAlignment::Model::Translation;
        source.alignmentDiagnostic.translationX=phase.translationRaw.x;
        source.alignmentDiagnostic.translationY=phase.translationRaw.y;
        source.alignmentDiagnostic.overlapFraction=phase.overlapFraction;
        source.alignmentDiagnostic.peakToSidelobeRatio=phase.peakToSidelobeRatio;
        source.alignmentDiagnostic.message="Global translation accepted.";
        if(request.recipe.alignmentMode==AlignmentMode::AutomaticGlobal||
           request.recipe.alignmentMode==AlignmentMode::AutomaticLocal) {
            const auto samples=ReferenceSamples(reference);
            Mfd::AffineRefinementOptions affineOptions;
            affineOptions.referenceExtent={data.width,data.height};affineOptions.alternateExtent={data.width,data.height};
            affineOptions.exposureScale=source.scale;affineOptions.minimumOverlapFraction=.55;
            affineOptions.singularValueMinimum=.97;affineOptions.singularValueMaximum=1.03;
            affineOptions.determinantMinimum=.94;affineOptions.determinantMaximum=1.06;
            affineOptions.maximumRotationDegrees=5;affineOptions.minimumSamples=128;
            const auto evaluator=[&source](Mfd::CfaSite,Mfd::RawCoordinate coordinate,Mfd::AffineSourceSample& sample) {
                double value=0;Mfd::RawSignalGradient gradient;
                if(!SampleProxySource(source,coordinate,value,&gradient)) return false;
                sample.value=value;sample.gradientRaw=gradient;
                sample.variance=1e-5+1e-4*std::max(0.0,value);return true;
            };
            Mfd::AffineRefinementResult affine;std::string ignored;
            if(Mfd::RefineGlobalAffine(samples,evaluator,source.alignment,affineOptions,affine,&ignored)&&affine.acceptedAffine) {
                source.alignment=affine.model;
                source.alignmentDiagnostic.model=CaptureAlignment::Model::GlobalAffine;
                source.alignmentDiagnostic.rotationDegrees=affine.rotationDegrees;
                source.alignmentDiagnostic.overlapFraction=affine.overlapFraction;
                source.alignmentDiagnostic.message="Global translation and rotation refinement accepted.";
            } else {
                source.alignmentDiagnostic.message="Translation accepted; rotation refinement had insufficient reliable evidence.";
            }
        }
        const double linearDelta=std::abs(source.alignment.linear[0]-1)+std::abs(source.alignment.linear[1])+std::abs(source.alignment.linear[2])+std::abs(source.alignment.linear[3]-1);
        if(std::hypot(source.alignment.translationRaw.x,source.alignment.translationRaw.y)<.08&&linearDelta<1e-4) {
            source.alignment={};source.alignment.centerRaw=center;
            source.alignmentDiagnostic.model=CaptureAlignment::Model::Identity;
            source.alignmentDiagnostic.translationX=source.alignmentDiagnostic.translationY=0;
            source.alignmentDiagnostic.rotationDegrees=0;
            source.alignmentDiagnostic.message="No measurable movement; identity coordinates retained.";
        } else {
            source.alignmentDiagnostic.translationX=source.alignment.translationRaw.x;
            source.alignmentDiagnostic.translationY=source.alignment.translationRaw.y;
            source.alignmentDiagnostic.rotationDegrees=RotationDegrees(source.alignment);
        }
        ObserveAlignment(request,data,source);
        ReportPresentation(request,ProcessingStage::GlobalAlignment,alternate,alternateCount,source.id);
        diagnostics.push_back(source.id+": alignment "+source.alignmentDiagnostic.message+
            " x="+std::to_string(source.alignmentDiagnostic.translationX)+
            " y="+std::to_string(source.alignmentDiagnostic.translationY)+
            " rotation="+std::to_string(source.alignmentDiagnostic.rotationDegrees)+" deg");
    }
    return true;
}

} // namespace Raw::Bracketing
