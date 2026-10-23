#include "CaptureReliability.h"
#include "CaptureCorrespondence.h"
#include "PhotometricReliability.h"
#include "PresentationEvidence.h"
#include "BorderSupport.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Raw::Bracketing {
namespace {
bool BuildReliability(const ProcessingRequest& request,
    const PreparedSource& reference,PreparedSource& source,const Mfd::Parameters& parameters,
    const Mfd::CfaPlanePyramid& referencePyramid,
    const Mfd::CfaPlanePyramid& sourcePyramid,
    const std::function<void(double)>& reportProgress,
    Mfd::ReliabilityMap& reliability,std::string& error) {
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(reference.frame.activeCfaPattern,layout)) {
        error="The local reliability CFA layout is unsupported.";return false;
    }
    Mfd::ReliabilityBuildRequest build;
    build.rawExtent=reference.frame.activeExtent;build.layout=layout;build.motionGrid=source.localMotion.get();
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
    // Every residual uses level zero of these immutable pyramids. Resolve the
    // four site descriptors once, rather than searching them for every cell.
    std::array<const Mfd::CfaPyramidLevel*,4> referenceLevels{},sourceLevels{};
    std::array<double,4> gradientNoiseScales{};
    const double scale=source.scale;
    const double scaleSquared=scale*scale;
    for(std::size_t s=0;s<referenceLevels.size();++s) {
        const auto site=static_cast<Mfd::CfaSite>(s);
        referenceLevels[s]=Mfd::FindCfaPyramidLevel(referencePyramid,site,0);
        sourceLevels[s]=Mfd::FindCfaPyramidLevel(sourcePyramid,site,0);
        if(sourceLevels[s]) {
            const double rawPixels=sourceLevels[s]->rawPixelsPerLevelPixel;
            if(std::isfinite(rawPixels)&&rawPixels>0)
                gradientNoiseScales[s]=scaleSquared/(rawPixels*rawPixels);
        }
    }
    build.residualEvaluator=[&](Mfd::RawCoordinate raw,Mfd::CfaSite site,
        const Mfd::LocalMotionFieldSample& local,Mfd::ReliabilityResidualSample& sample) {
        sample={};sample.valid=true;
        const auto siteIndex=static_cast<std::size_t>(site);
        if(siteIndex>=referenceLevels.size())return false;
        const auto* referenceLevel=referenceLevels[siteIndex];
        const auto* sourceLevel=sourceLevels[siteIndex];
        if(!referenceLevel||!sourceLevel||raw.x<0||raw.y<0) return false;

        const auto referencePlane=layout.RawToPlane(raw,site);
        const auto referenceX=static_cast<std::int64_t>(std::llround(referencePlane.x));
        const auto referenceY=static_cast<std::int64_t>(std::llround(referencePlane.y));
        if(referenceX<0||referenceY<0||
           referenceX>=static_cast<std::int64_t>(referenceLevel->extent.width)||
           referenceY>=static_cast<std::int64_t>(referenceLevel->extent.height)) return true;
        const auto referenceIndex=static_cast<std::size_t>(referenceY)*referenceLevel->extent.width+
            static_cast<std::size_t>(referenceX);
        const bool referenceClipped=!referenceLevel->clippedMask.empty()&&referenceLevel->clippedMask[referenceIndex];
        if(!referenceLevel->validMask[referenceIndex]&&!referenceClipped) return true;
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
        const double rawPixelsPerPlanePixel=sourceLevel->rawPixelsPerLevelPixel;
        if(!std::isfinite(scale)||scale<=0||!std::isfinite(rawPixelsPerPlanePixel)||
           rawPixelsPerPlanePixel<=0) return true;
        const double gradientRawX=scale*gradientPlaneX/rawPixelsPerPlanePixel;
        const double gradientRawY=scale*gradientPlaneY/rawPixelsPerPlanePixel;
        const double gradientNoiseScale=gradientNoiseScales[siteIndex];
        const double registrationNoise=gradientNoiseScale*(
            gradientNoiseXX*local.covarianceRaw.xxRawPixelsSquared+
            2*gradientNoiseXY*local.covarianceRaw.xyRawPixelsSquared+
            gradientNoiseYY*local.covarianceRaw.yyRawPixelsSquared);
        const double registrationVariance=std::max(0.0,
            gradientRawX*gradientRawX*local.covarianceRaw.xxRawPixelsSquared+
            2*gradientRawX*gradientRawY*local.covarianceRaw.xyRawPixelsSquared+
            gradientRawY*gradientRawY*local.covarianceRaw.yyRawPixelsSquared-registrationNoise);
        const double exposureVariance=sourceValue*sourceValue*source.scaleVariance;
        sourceValue*=scale;sourceVariance*=scaleSquared;
        const double alternateVariance=sourceVariance+registrationVariance+exposureVariance+
            parameters.fusion.numericalVarianceFloor;
        if(!std::isfinite(sourceValue)||!std::isfinite(alternateVariance)||alternateVariance<=0)
            return true;
        sample.hardValid=true;sample.referenceValue=referenceValue;
        sample.referenceIsLowerBound=referenceClipped;
        sample.alternateValue=sourceValue;sample.referenceGateVariance=referenceVariance;
        sample.alternateGateVariance=alternateVariance;return true;
    };
    return BuildColorReliability(build,reliability,error,[&](Mfd::RawCoordinate point,Mfd::LocalMotionFieldSample& sample) {
        return EvaluateCaptureMotion(source,point,build.motionOptions,sample);
    });
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

Mfd::Parameters CaptureAlignmentParameters(const PreparedDataset& data) {
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
    return parameters;
}

bool RefreshCaptureReliability(const ProcessingRequest& request,const PreparedSource& reference,
    PreparedSource& source,const Mfd::Parameters& parameters,
    const Mfd::CfaPlanePyramid& referencePyramid,const Mfd::CfaPlanePyramid& sourcePyramid,
    const std::function<void(double)>& reportProgress,std::string& error) {
    if(!source.localMotion) {error="Reliability needs a capture motion field.";return false;}
    Mfd::ReliabilityMap reliability;
    if(!BuildReliability(request,reference,source,parameters,referencePyramid,sourcePyramid,
        reportProgress,reliability,error))return false;
    auto store=std::make_shared<Mfd::ReliabilityStore>();
    if(!Mfd::BuildReliabilityStore(reliability,Mfd::ReliabilityStorageFormat::Uint16Unorm,
        parameters.reliability.storageTileCells,*store,&error))return false;
    auto inspection=MakeInspection(reliability);
    source.reliability=std::move(store);source.alignmentInspection=inspection;
    StampCaptureReliability(source);
    std::size_t acceptedCells=0,uncertainCells=0,rejectedCells=0;
    for(std::size_t i=0;i<reliability.cells.size();++i) {
        const auto& cell=reliability.cells[i];
        if(HardCaptureRejection(cell.rejectionBits)) ++rejectedCells;
        // Report the same confidence precision as the published inspection.
        // Interpolated 0.20-confidence fallback nodes can land a double ULP
        // below 0.20; classifying before conversion made identical displayed
        // evidence arbitrarily switch between accepted and uncertain.
        else if(inspection->confidence[i]>=float(parameters.reliability.frameUsableReliabilityThreshold)) ++acceptedCells;
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
        :"Automatic local alignment accepted.";
    return true;
}
}
