#include "ProcessingInternal.h"
#include "PresentationEvidence.h"
#include "Alignment.h"
#include "CaptureSharpness.h"
#include "BurstAlignment.h"
#include "LocalDetail.h"
#include "HdrColorFusion.h"
#include "Raw/RawLoader.h"
#include "Panorama/Panorama.h"
#include "Persistence/RawProjectModel.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Raw::Bracketing {
std::string AnalysisIdentity(const ProcessingRequest& request) {
    if(request.recipe.reconstruction==ReconstructionMode::Panorama)return Panorama::PreparationIdentity(request);
    auto recipe=request.recipe;recipe.knots=EqualCurves(recipe.groups.size());recipe.automatic=true;
    // Output resolution does not alter source preparation, motion or the guide.
    recipe.reconstruction=ReconstructionMode::Standard;
    for(auto& group:recipe.groups) {group.name.clear();group.colorSlot=-1;}
    auto j=Serialize(recipe);
    // Sampling and clipping decisions feed reliability, the guide and cached
    // group tiles. Keep their revisions together when these decisions change.
    j["alignmentProfile"]="automatic-local-v19-highlight-geometry";
    j["detailProfile"]=kLocalDetailRevision;
    j["hdrProfile"]=kHdrColorFusionRevision;
    for(const auto& s:request.sources) j["sources"].push_back({s.frameId,s.sha256,s.bytes});
    // Native inspection uses the immutable prepared captures and does not
    // need to re-extract project assets merely to name the same sources.
    if(request.sources.empty()&&request.analysis) {
        const auto stored=nlohmann::json::parse(request.analysis->identity,nullptr,false);
        if(stored.is_object()&&stored.contains("sources"))j["sources"]=stored["sources"];
    }
    return j.dump();
}
bool Prepare(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    data.directory=request.cacheDirectory;
    data.removeCacheOnRelease=request.removeCacheOnRelease;
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    std::unordered_set<std::string> identities;
    std::size_t captureTotal=0;
    for(const auto& group:request.recipe.groups)for(const auto& frame:group.frames)
        if((group.enabled&&frame.enabled)||frame.id==request.recipe.originFrameId)++captureTotal;
    const auto preparationStart=std::chrono::steady_clock::now();
    for(std::size_t g=0;g<request.recipe.groups.size();++g) for(const auto& f:request.recipe.groups[g].frames) {
        if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
        const bool enabled=f.enabled&&request.recipe.groups[g].enabled;
        if(!enabled&&f.id!=request.recipe.originFrameId) continue;
        const auto source=std::find_if(request.sources.begin(),request.sources.end(),[&](const auto& s){return s.frameId==f.id;});
        if(source==request.sources.end()) {error="Missing source "+f.id;return false;}
        if(!identities.insert(source->sha256).second) {error="Duplicate source content cannot supply independent measurements.";return false;}
        ReportPresentation(request,ProcessingStage::Preparing,data.sources.size(),captureTotal,f.id);
        RawImageData raw;
        if(request.reportProgress) request.reportProgress(.02,"Preparing capture "+std::to_string(data.sources.size()+1));
        // Check metadata before the decoder allocates its full sensor buffers.
        RawMetadata metadata;
        if(!request.decode && RawLoader::LoadMetadata(source->path.string(),metadata)) {
            const auto pixels=static_cast<std::uint64_t>(std::max(0,metadata.rawWidth))*std::max(0,metadata.rawHeight);
            if(pixels>request.memoryBudgetBytes/48) {error="The RAW decoder and output exceed the available bracketing memory budget.";return false;}
        }
        const bool loaded=request.decode?request.decode(source->path,raw,request.shouldCancel):RawLoader::LoadFile(source->path.string(),raw,request.shouldCancel);
        if(!loaded) {error=raw.metadata.error.empty()?"Could not decode a bracket capture.":raw.metadata.error;return false;}
        if(raw.metadata.sourceContentSha256!=source->sha256 || raw.metadata.sourceByteSize!=source->bytes) {
            error="A bracket capture no longer matches its saved content identity.";return false;
        }
        const auto pixels=static_cast<std::uint64_t>(std::max(0,raw.metadata.rawWidth))*std::max(0,raw.metadata.rawHeight);
        if(pixels>request.memoryBudgetBytes/48) {
            error="This dataset exceeds the available bracketing memory budget.";return false;
        }
        raw.metadata.orientation = ResolveOrientation(request.recipe, raw.metadata.orientation);
        if(!data.sources.empty()) {
            auto reference=Stack::Project::BuildRawCaptureCompatibilitySummary(data.sources.front().metadata);
            auto candidate=Stack::Project::BuildRawCaptureCompatibilitySummary(raw.metadata);
            if(!Stack::Project::AreHdrCapturesStructurallyCompatible(reference,candidate,&error,&diagnostics)) return false;
        }
        Mfd::PreparationOptions options;options.shouldCancel=request.shouldCancel;
        const auto bytesPerTilePixel=request.recipe.groups.size()*(sizeof(Observation)+2*sizeof(double))+48;
        while(options.tileRawPixels>64&&static_cast<std::uint64_t>(options.tileRawPixels)*options.tileRawPixels*bytesPerTilePixel>request.memoryBudgetBytes/8)
            options.tileRawPixels/=2;
        auto prepared=Mfd::PrepareRawFrame(raw,options,cache);
        if(!prepared.success) {error=prepared.message;return false;}
        PreparedSource s;s.id=f.id;s.group=g;s.enabled=enabled;s.metadata=raw.metadata;s.frame=std::move(prepared.frame);
        Mfd::NoiseResolutionOptions noiseOptions;
        noiseOptions.enableGenericLowConfidence=true;
        const double iso=std::max(100.0,static_cast<double>(raw.metadata.isoSpeed));
        for(auto& site:noiseOptions.genericLowConfidenceSites) {site.shotScale=1e-4*iso/100;site.offsetVariance=1e-7*iso*iso/10000;}
        const auto noise=Mfd::ResolveNoiseModel(s.metadata,s.frame,&cache,noiseOptions);
        if(!noise.resolved) {error="Noise estimation failed for "+f.id+": "+noise.message;return false;}
        s.noise=noise.model;
        diagnostics.push_back(f.id+": "+Mfd::NoiseModelQualityName(s.noise.quality));
        const auto width=static_cast<unsigned>(s.frame.activeExtent.width),height=static_cast<unsigned>(s.frame.activeExtent.height);
        if(data.sources.empty()) {data.width=width;data.height=height;}
        if(width!=data.width||height!=data.height||width%2||height%2) {error="Bracketing requires matching, even-sized Bayer active areas.";return false;}
        if(f.id==request.recipe.originFrameId) data.origin=data.sources.size();
        // Bound all original overview images together, independent of capture count.
        const auto captureCount=std::max<std::size_t>(1,request.sources.size());
        unsigned previewStride=2;
        while(static_cast<std::uint64_t>((width+previewStride-1)/previewStride)*((height+previewStride-1)/previewStride)*13*captureCount>8*1024*1024) previewStride+=2;
        s.original=std::make_shared<CapturePreview>();
        s.proxyStride=previewStride;
        auto& original=*s.original;original.width=(width+previewStride-1)/previewStride;original.height=(height+previewStride-1)/previewStride;
        original.rgb.resize(static_cast<std::size_t>(original.width)*original.height*3);
        original.clipped.resize(static_cast<std::size_t>(original.width)*original.height);
        Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(s.frame.activeCfaPattern,layout);
        for(unsigned ty=0;ty<s.frame.tileRows;++ty) for(unsigned tx=0;tx<s.frame.tileColumns;++tx) {
            Mfd::PreparedRawTile tile;
            if(Mfd::ReadPreparedTile(s.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit) return false;
            for(unsigned y=0;y<tile.extent.height;++y) for(unsigned x=0;x<tile.extent.width;++x) {
                const auto gx=tile.originX+x,gy=tile.originY+y;
                if(gx%previewStride<=1&&gy%previewStride<=1) {
                    const auto q=(gy/previewStride)*original.width+gx/previewStride;
                    const auto sample=y*tile.extent.width+x;
                    const auto site=layout.SiteAt(gx,gy);
                    const unsigned channel=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
                    const auto v=tile.normalizedMosaic[sample]*tile.comparisonGain[sample];
                    original.rgb[q*3+channel]+=std::isfinite(v)?v*(channel==1?.5f:1.f):0;
                    original.clipped[q]|=Mfd::HasSampleFlag(tile.sampleFlags[sample],Mfd::PreparedSampleFlag::Saturated)||Mfd::HasSampleFlag(tile.sampleFlags[sample],Mfd::PreparedSampleFlag::ExplicitDecoderClip);
                }
            }
        }
        if(request.reportPresentation) {
            auto image=std::make_shared<ProcessingThumbnail>();image->frameId=f.id;image->group=static_cast<unsigned>(g);
            const unsigned step=std::max(1u,(std::max(original.width,original.height)+511)/512);
            image->width=(original.width+step-1)/step;image->height=(original.height+step-1)/step;
            image->rgba.resize(static_cast<std::size_t>(image->width)*image->height*4);
            const auto& display=data.sources.empty()?s.metadata:data.sources.front().metadata;
            for(unsigned y=0;y<image->height;++y)for(unsigned x=0;x<image->width;++x) {
                float camera[3];const auto i=(y*step*original.width+x*step)*3;
                for(unsigned c=0;c<3;++c)camera[c]=original.rgb[i+c]*display.cameraWhiteBalance[c]/std::max(1e-6f,display.cameraWhiteBalance[1]);
                const auto q=(y*image->width+x)*4;
                for(unsigned c=0;c<3;++c) {
                    float v=0;for(unsigned k=0;k<3;++k)v+=display.cameraToSrgb[c*3+k]*camera[k];
                    v=std::isfinite(v)?std::max(0.f,v):0;v/=1+v;
                    v=v<=.0031308f?12.92f*v:1.055f*std::pow(v,1.f/2.4f)-.055f;
                    image->rgba[q+c]=static_cast<std::uint8_t>(std::clamp(v,0.f,1.f)*255+.5f);
                }image->rgba[q+3]=255;
            }
            ProcessingProgress event;event.stage=ProcessingStage::Preparing;event.captureId=f.id;
            event.completed=data.sources.size()+1;event.total=captureTotal;event.thumbnail=image;
            event.detail="ISO "+std::to_string(s.metadata.isoSpeed)+"  /  "+std::to_string(s.metadata.exposureTimeSeconds)+" s";
            request.reportPresentation(event);
            s.presentationThumbnail=image;
        }
        data.sources.push_back(std::move(s));
    }
    data.stageSeconds["decode_prepare"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-preparationStart).count();
    {
        ProcessingTimer timer(data.stageSeconds,"global_alignment");
        if(!Align(request,data,diagnostics,error)) return false;
    }
    ReportPresentation(request,ProcessingStage::Noise);
    if(request.reportProgress) request.reportProgress(.09,"Measuring burst noise");
    {
        ProcessingTimer timer(data.stageSeconds,"noise_calibration");
        if(!RefineBurstNoise(request,data,diagnostics,error)) return false;
    }
    {
        ProcessingTimer timer(data.stageSeconds,"exposure_calibration");
        ReportPresentation(request,ProcessingStage::Exposure);
        if(!Calibrate(request,data,diagnostics,error)) return false;
    }
    const bool refineHdr=request.recipe.alignmentMode==AlignmentMode::AutomaticLocal&&request.recipe.groups.size()>1;
    auto localRequest=request;
    if(refineHdr&&request.reportProgress) localRequest.reportProgress=[&](double progress,const std::string& stage) {
        request.reportProgress(.10+.36*std::clamp((progress-.10)/.42,0.,1.),stage);
    };
    if(!AlignLocal(localRequest,data,diagnostics,error)) return false;
    if(refineHdr) {
        const auto beforeCount=diagnostics.size();std::vector<double> previousScales,previousVariance;
        for(const auto& source:data.sources) {previousScales.push_back(source.scale);previousVariance.push_back(source.scaleVariance);}
        // Revisit the joint solve once correspondence is known. This step is
        // bounded and retains the earlier calibrated solve if local overlap
        // is inadequate. It never publishes metadata-only HDR calibration.
        ReportPresentation(request,ProcessingStage::Exposure);
        if(!Calibrate(request,data,diagnostics,error)) {
            if(request.shouldCancel&&request.shouldCancel()) return false;
            diagnostics.resize(beforeCount);error.clear();
            diagnostics.push_back("Local overlap could not connect every exposure; retained the calibrated global-overlap scales.");
        } else {
            bool changed=false;
            for(std::size_t i=0;i<data.sources.size();++i)
                changed|=std::abs(std::log2(data.sources[i].scale/previousScales[i]))>1e-6||
                    std::abs(data.sources[i].scaleVariance-previousVariance[i])>1e-12;
            if(changed) {
                diagnostics.push_back("Updated HDR exposure scales from local correspondence; refreshing confidence without repeating motion search.");
                if(request.reportProgress) localRequest.reportProgress=[&](double progress,const std::string& stage) {
                    request.reportProgress(.46+.06*std::clamp((progress-.10)/.42,0.,1.),"Refreshing HDR alignment confidence");
                };
                ++localRequest.presentationPass;
                if(!AlignLocal(localRequest,data,diagnostics,error)) return false;
            }
        }
    }
    if(!RefineBurstAlignment(request,data,diagnostics,error))return false;
    return MeasureCaptureSharpness(request,data,diagnostics,error);
}
} // namespace Raw::Bracketing
