#include "../HdrColorFusion.h"
#include "Reconstruction.h"
#include "../PresentationEvidence.h"
#include "Preparation.h"
#include "Memory.h"
#include "Raw/RawProcessingMath.h"
#include "Persistence/RawProjectModel.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace Raw::Bracketing {
BracketingResult::ReconstructionInspection::~ReconstructionInspection() {
    if(!directory.empty()){std::error_code e;std::filesystem::remove_all(directory,e);}
}
namespace {
struct GpuSession {
    const ProcessingRequest& request;
    Sr::GpuKernel kernel;
    bool enabled=false,used=false,verified=false;
    std::string fallback;
    explicit GpuSession(const ProcessingRequest& r):request(r),enabled(bool(r.executeOpenGlTask)){}
    ~GpuSession(){if(request.executeOpenGlTask){std::string e;request.executeOpenGlTask([&](std::string&){kernel.Release();return true;},e);}}
    bool Call(const Raw::OpenGlTask& task) {
        if(!enabled)return false;
        std::string error;
        if(request.executeOpenGlTask(task,error))return true;
        enabled=false;fallback=error;return false;
    }
};
// A missing reconstructed color uses the nearest temporal CFA measurement of
// that color, including its actual group weights and clipping reason. Avoid a
// demosaic fallback whose neighboring contributions cannot be accounted for.
struct NativeFallback {
    Mfd::PreparedRawTile tile;
    std::vector<std::vector<Observation>> groups;
    Pixel Read(const ProcessingRequest& request,const PreparedDataset& data,
        unsigned x,unsigned y,unsigned channel,const std::vector<double>& shares,std::string& error) {
        Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(data.sources[data.origin].frame.activeCfaPattern,layout);
        unsigned selectedX=x,selectedY=y,best=10;
        for(unsigned s=0;s<4;++s) {
            const auto site=layout.SiteAt(s%2,s/2);
            const unsigned color=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
            const unsigned xx=(x&~1u)+s%2,yy=(y&~1u)+s/2;
            const unsigned distance=unsigned(std::abs(int(xx)-int(x))+std::abs(int(yy)-int(y)));
            if(color==channel&&distance<best){selectedX=xx;selectedY=yy;best=distance;}
        }
        const unsigned side=data.sources[data.origin].frame.tileRawPixels;
        if(groups.empty()||selectedX<tile.originX||selectedY<tile.originY||
           selectedX>=tile.originX+tile.extent.width||selectedY>=tile.originY+tile.extent.height) {
            if(!ReadGroups(request,data,selectedX/side,selectedY/side,groups,tile,error))return {};
        }
        const auto p=std::size_t((selectedY&~1u)-tile.originY)*tile.extent.width+(selectedX&~1u)-tile.originX;
        ColorObservations values;GatherColorCell(groups,p,tile.extent.width,values);
        auto blended=BlendColor(values,shares,request.recipe)[(selectedY%2)*2+selectedX%2];
        blended.fallback=true;
        if(blended.fallbackReason==MeasurementFallbackReason::None)blended.fallbackReason=MeasurementFallbackReason::AutomaticValidInput;
        return blended;
    }
};
bool RunGroup(const ProcessingRequest& request,const BracketingAnalysis& analysis,std::size_t group,
    Sr::Tile& tile,PreparedTileSampler& sampler,GpuSession& gpu,std::string& error) {
    const auto& data=*analysis.prepared;
    const Sr::Tile initial=tile;
    const auto observe=[&] {try {
        if(!request.evidenceStream||tile.pixels.empty())return;
        ProcessingEvidence e;e.caption="Measured reconstruction kernels";
        const auto step=std::max<std::size_t>(1,tile.pixels.size()/24);
        for(std::size_t i=0;i<tile.pixels.size()&&e.kernels.size()<24;i+=step) {
            const auto& k=tile.pixels[i].kernel;const float det=k.x*k.z-k.y*k.y;
            if(det<=1e-12f)continue;
            e.kernels.push_back({(tile.originX+float(i%tile.width)*tile.step)/data.width,
                (tile.originY+float(i/tile.width)*tile.step)/data.height,
                k.z/det/(float(data.width)*data.width),-k.y/det/(float(data.width)*data.height),
                k.x/det/(float(data.height)*data.height)});
        }
        e.metrics={{"Sampling step","sensor px",tile.step},{"Group","",double(group+1)}};
        request.evidenceStream->Details(e);
        }catch(...) { /* Observation must not change reconstruction. */ }
    };
    bool accelerated=gpu.Call([&](std::string& e){return gpu.kernel.Begin(tile,e);});
    constexpr unsigned probes=17;
    std::array<std::size_t,probes> indices{};std::array<Sr::PixelAccumulator,probes> expected{};
    for(unsigned i=0;i<probes;++i){indices[i]=i*(tile.pixels.size()-1)/(probes-1);expected[i]=tile.pixels[indices[i]];}
    const auto run=[&](bool useGpu)->bool {
        for(const auto& source:data.sources)if(source.enabled&&source.group==group) {
            Sr::FrameRegion frame;
            if(!Sr::BuildFrameRegion(analysis,source,tile,sampler,frame,request.shouldCancel,error))return false;
            if(useGpu) {
                if(!gpu.verified)for(unsigned i=0;i<probes;++i)
                    Sr::AccumulatePixel(tile,frame,unsigned(indices[i]%tile.width),unsigned(indices[i]/tile.width),expected[i]);
                if(!gpu.Call([&](std::string& e){return gpu.kernel.Add(tile,frame,e);} ))return false;
            } else if(!Sr::AccumulateCpu(tile,frame,request.shouldCancel,error,request.workerCount))return false;
        }
        return true;
    };
    if(accelerated) {
        accelerated=run(true)&&gpu.Call([&](std::string& e){return gpu.kernel.Read(tile,e);});
        if(accelerated&&!gpu.verified) {
            for(unsigned i=0;i<probes;++i)for(unsigned c=0;c<3;++c) {
                const auto a=Sr::Resolve(tile.pixels[indices[i]],c),b=Sr::Resolve(expected[i],c);
                if(a.valid!=b.valid||!std::isfinite(a.value)||std::abs(a.value-b.value)>2e-5f*std::max(1.f,std::abs(b.value))||
                   std::abs(a.support-b.support)>1e-3f*std::max(1.f,b.support))accelerated=false;
            }
            if(!accelerated){gpu.enabled=false;gpu.fallback="CPU verification rejected GPU reconstruction.";}
            else gpu.verified=true;
        }
        if(accelerated){gpu.used=true;observe();return true;}
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        if(!error.empty())return false;
        tile=initial;
    }
    const bool complete=run(false);if(complete)observe();return complete;
}
}
bool ReconstructSuperResolution(const ProcessingRequest& request,BracketingResult& result,std::string& error) {
    if(request.recipe.reconstruction!=ReconstructionMode::SuperResolution1x&&
       request.recipe.reconstruction!=ReconstructionMode::SuperResolution2x)return true;
    if(!result.raw||!result.analysis||!result.analysis->prepared){error="Super-resolution requires prepared captures.";return false;}
    const auto start=std::chrono::steady_clock::now();
    const auto& data=*result.analysis->prepared;const auto& native=*result.raw;
    const unsigned scale=request.recipe.reconstruction==ReconstructionMode::SuperResolution2x?2:1;
    const unsigned width=data.width*scale,height=data.height*scale;const auto groups=request.recipe.groups.size();
    const std::uint64_t pixels=std::uint64_t(width)*height;
    if(pixels>std::numeric_limits<std::size_t>::max()/28) {
        error="Super-resolution dimensions exceed the addressable output size.";return false;
    }
    // Keep the bounded temporal cells for interactive contribution edits.
    // Completed inspection still uses reconstructed RGB and evaluated weights.
    // Float RGB, per-pixel evidence, the retained native result, and bounded
    // CPU/GPU tiles are all live during reconstruction.
    std::uint64_t analysisBytes=result.analysis->guideEv.capacity()*sizeof(float);
    for(const auto& source:data.sources) {
        if(source.original)analysisBytes+=source.original->rgb.capacity()*sizeof(float)+source.original->clipped.capacity();
        if(source.refinedMotion)analysisBytes+=source.refinedMotion->cells.capacity()*sizeof(MotionCorrection);
        if(source.localMotion)analysisBytes+=source.localMotion->nodes.capacity()*sizeof(Mfd::MotionNode);
        if(source.reliability)for(const auto& t:source.reliability->tiles)
            analysisBytes+=t.uint16Values.capacity()*2+t.floatValues.capacity()*4+t.rejectionBits.capacity()*2;
        if(source.alignmentInspection)analysisBytes+=source.alignmentInspection->confidence.capacity()*4+source.alignmentInspection->rejectionBits.capacity()*2;
    }
    const auto& p=result.preview;
    analysisBytes+=(p.resultRgb.capacity()+p.sourceRgb.capacity()+p.contributions.capacity()+p.requested.capacity()+p.guideEv.capacity())*4+p.diagnostics.capacity();
    analysisBytes+=p.samples.capacity()*sizeof(Preview::Sample);
    const auto sourceTile=data.sources[data.origin].frame.tileRawPixels;
    const auto fallbackBytes=std::uint64_t(sourceTile)*sourceTile*(groups*(sizeof(Observation)+32)+80);
    Sr::MemoryRequirements requirements;
    requirements.residentBytes=std::uint64_t(data.width)*data.height*20+analysisBytes;
    requirements.outputBytes=pixels*28;
    requirements.workingBytes=fallbackBytes+128ull*1024*1024+
        result.analysis->guideEv.capacity()*sizeof(float)+p.guideEv.size()*sizeof(unsigned);
    requirements.sourceTileBytes=std::uint64_t(sourceTile)*sourceTile*9;
    requirements.preferredCacheTiles=data.sources.size()*4+4;
    requirements.bytesPerOutputTilePixel=groups*3*sizeof(Sr::ChannelResult)+3*sizeof(Sr::PixelAccumulator)+groups*4;
    const auto memoryPlan=Sr::PlanMemory(requirements,request.memoryBudgetBytes,
        request.automaticMemoryBudget,Mfd::QueryPhysicalMemorySnapshot());
    if(!memoryPlan.fits){error=memoryPlan.message;return false;}
    const unsigned tileSide=memoryPlan.tileSide;
    const auto cacheCount=memoryPlan.cacheTiles;
    auto raw=std::make_shared<RawImageData>();raw->metadata=native.metadata;
    auto& metadata=raw->metadata;
    metadata.rawWidth=metadata.visibleWidth=width;metadata.rawHeight=metadata.visibleHeight=height;
    metadata.pixelLayout=RawPixelLayout::LinearRgb;metadata.mosaiced=false;metadata.cfaPattern=CfaPattern::Unknown;
    metadata.linearChannels=3;metadata.linearSampleFormat=RawSampleFormat::Float32;
    metadata.dngActiveArea={0,0,int(height),int(width)};
    metadata.dngGainMaps.clear();metadata.dngGainMapCount=0;
    metadata.dngLinearizationTable.clear();metadata.dngBlackLevelValues.clear();
    metadata.dngBlackLevelDeltaH.clear();metadata.dngBlackLevelDeltaV.clear();
    metadata.blackLevel=0;metadata.perChannelBlack={0,0,0,0};metadata.whiteLevel=1;
    raw->reconstructedCameraRgb=true;
    raw->contentIdentity=result.identity;raw->contentIdentityHash=native.contentIdentityHash;
    metadata.sourceByteSize=pixels*3*sizeof(float);
    raw->linearFloatBuffer.resize(std::size_t(pixels)*3);
    auto variance=std::make_shared<std::vector<float>>(pixels);
    auto uncertainty=std::make_shared<std::vector<float>>(pixels);
    auto support=std::make_shared<std::vector<float>>(pixels);
    auto validity=std::make_shared<std::vector<std::uint8_t>>(pixels);
    auto clipping=std::make_shared<std::vector<std::uint8_t>>(pixels);
    auto rejection=std::make_shared<std::vector<std::uint8_t>>(pixels);
    auto fallback=std::make_shared<std::vector<std::uint8_t>>(pixels);
    auto inspection=std::make_shared<BracketingResult::ReconstructionInspection>();
    inspection->directory=data.directory/("sr-"+Stack::Project::GenerateStableUuid());
    inspection->scale=scale;inspection->tilePixels=tileSide;inspection->sensorWidth=data.width;inspection->sensorHeight=data.height;inspection->groups=unsigned(groups);
    inspection->recipe=request.recipe;
    std::filesystem::create_directories(inspection->directory);
    const float diversity=Sr::SamplingDiversity(data);const bool diverse=diversity>=3;
    // The planner bounded both the source cache and output tile before any
    // full-resolution allocation. Keep neighboring sensor tiles across tiles.
    GpuSession gpu(request);PreparedTileSampler sampler(data.directory,cacheCount);NativeFallback nativeFallback;
    Preview preview=std::move(result.preview);
    std::vector<unsigned> previewCounts(preview.guideEv.size());
    std::fill(preview.resultRgb.begin(),preview.resultRgb.end(),0.f);
    std::fill(preview.contributions.begin(),preview.contributions.end(),0.f);
    std::fill(preview.diagnostics.begin(),preview.diagnostics.end(),0);
    const auto enabled=std::count_if(data.sources.begin(),data.sources.end(),[](const auto& s){return s.enabled;});
    result.invalidSamples=result.fallbackSamples=result.unrecoverableSamples=0;
    if(request.evidenceStream)request.evidenceStream->Begin(request,ProcessingStage::Reconstruction,width,height,raw->metadata);
    for(unsigned top=0;top<height;top+=tileSide)for(unsigned left=0;left<width;left+=tileSide) {
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        const auto tw=std::min(tileSide,width-left),th=std::min(tileSide,height-top);const auto count=std::size_t(tw)*th;
        std::vector<std::vector<std::array<Sr::ChannelResult,3>>> groupResults(groups);
        for(std::size_t g=0;g<groups;++g) {
            auto tile=Sr::MakeTile(left,top,tw,th,scale,*result.analysis,diverse);
            if(!RunGroup(request,*result.analysis,g,tile,sampler,gpu,error))return false;
            groupResults[g].resize(count);
            for(std::size_t p=0;p<count;++p)for(unsigned c=0;c<3;++c)groupResults[g][p][c]=Sr::Resolve(tile.pixels[p],c);
        }
        std::vector<float> actual(count*groups);ColorObservations observations;
        for(auto& site:observations)site.resize(groups);
        for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x) {
            const auto p=std::size_t(y)*tw+x,index=std::size_t(top+y)*width+left+x;
            const unsigned sx=std::min(data.width-1,(left+x)/scale),sy=std::min(data.height-1,(top+y)/scale);
            const auto nativeIndex=std::size_t(sy)*data.width+sx;
            const float ev=result.analysis->guideEv[(sy/2)*(data.width/2)+sx/2];
            const auto shares=Evaluate(request.recipe.automatic?result.analysis->suggestion:request.recipe.knots,ev);
            bool valid=true,didFallback=false,clipped=false;float minSupport=float(enabled),maxVariance=0,maxUncertainty=0;
            auto reason=MeasurementFallbackReason::None;
            for(unsigned c=0;c<3;++c) {
                for(std::size_t g=0;g<groups;++g) {
                    const auto& channel=groupResults[g][p][c];auto& o=observations[c][g];o={};
                    o.value=channel.value;o.measurementVariance=channel.variance;o.uncertaintyVariance=channel.uncertainty;
                    o.variance=o.measurementVariance+o.uncertaintyVariance;
                    o.support=channel.valid?channel.support:0;o.headroom=1;o.finite=channel.valid;
                }
            }
            const auto color=BlendColor(observations,shares,request.recipe,3);
            for(unsigned c=0;c<3;++c) {
                auto blended=color[c];
                if(!blended.valid) {
                    blended=nativeFallback.Read(request,data,sx,sy,c,shares,error);
                    if(!error.empty())return false;
                }
                valid&=blended.valid;clipped|=blended.clipped;didFallback|=blended.fallback;
                if(unsigned(blended.fallbackReason)>unsigned(reason))reason=blended.fallbackReason;
                minSupport=std::min(minSupport,float(blended.support));maxVariance=std::max(maxVariance,float(blended.measurementVariance));
                maxUncertainty=std::max(maxUncertainty,float(blended.uncertaintyVariance));
                raw->linearFloatBuffer[index*3+c]=std::isfinite(blended.value)?float(blended.value):0;
                for(std::size_t g=0;g<groups;++g)actual[p*groups+g]+=float(blended.actual[g]/3);
            }
            (*variance)[index]=maxVariance;(*support)[index]=minSupport;(*validity)[index]=valid;
            (*uncertainty)[index]=maxUncertainty;
            (*clipping)[index]=clipped;
            (*rejection)[index]=(*native.multiFrameMeasurementSidecars->localRejection)[nativeIndex];
            (*fallback)[index]=std::uint8_t(reason);
            result.invalidSamples+=!valid&&!clipped;result.fallbackSamples+=didFallback;result.unrecoverableSamples+=clipped;
            // Area reduction of the evaluated output, including its actual
            // contributions. Never run a different lower-quality merge.
            const unsigned stride=unsigned(preview.sensorStepX);
            {
                const auto q=std::size_t(sy/stride)*preview.width+sx/stride;
                if(q<preview.guideEv.size()) {
                    ++previewCounts[q];
                    for(unsigned c=0;c<3;++c)preview.resultRgb[q*3+c]+=raw->linearFloatBuffer[index*3+c];
                    for(std::size_t g=0;g<groups;++g)preview.contributions[q*groups+g]+=actual[p*groups+g];
                    preview.diagnostics[q]|=(didFallback?Preview::Fallback:0)|(!valid&&!clipped?Preview::Invalid:0)|(clipped?Preview::Unrecoverable:0)|
                        (reason==MeasurementFallbackReason::ShortestExposure?Preview::ShortestExposure:0)|
                        (reason==MeasurementFallbackReason::FixedReference?Preview::FixedReference:0);
                }
            }
        }
        if(request.evidenceStream)request.evidenceStream->Tile(request,ProcessingStage::Reconstruction,left,top,tw,th,[&](unsigned x,unsigned y) {
            PresentationSample s;const auto index=std::size_t(y)*width+x;
            s.rgb={raw->linearFloatBuffer[index*3],raw->linearFloatBuffer[index*3+1],raw->linearFloatBuffer[index*3+2]};
            const auto group=PresentationGroup(request);
            s.first=actual[(std::size_t(y-top)*tw+x-left)*groups+group];
            const auto ev=result.analysis->guideEv[((y/scale)/2)*(data.width/2)+(x/scale)/2];
            s.second=float(Evaluate(request.recipe.automatic?result.analysis->suggestion:request.recipe.knots,ev)[group]);
            s.known=(*validity)[index]!=0;return s;
        });
        std::ofstream file(inspection->directory/(std::to_string(left/tileSide)+"-"+std::to_string(top/tileSide)+".bin"),std::ios::binary);
        file.write(reinterpret_cast<const char*>(actual.data()),std::streamsize(actual.size()*sizeof(float)));
        if(!file){error="Could not write super-resolution inspection data.";return false;}
        inspection->checksums[{left/tileSide,top/tileSide}]=Stack::NodeMath::Sha256ContentIdentity(
            std::vector<std::string_view>{std::string_view(reinterpret_cast<const char*>(actual.data()),actual.size()*sizeof(float))});
        ReportPresentation(request,ProcessingStage::Reconstruction,std::uint64_t(top)*width+std::uint64_t(left+tw)*th,pixels,{},std::to_string(width)+" x "+std::to_string(height));
        if(request.reportProgress)request.reportProgress(.90+.10*double(std::uint64_t(top)*width+std::uint64_t(left+tw)*th)/pixels,"Reconstructing super-resolution "+std::to_string(scale)+"x");
    }
    if(request.evidenceStream)request.evidenceStream->Flush(request);
    for(std::size_t q=0;q<previewCounts.size();++q)if(previewCounts[q]) {
        for(unsigned c=0;c<3;++c)preview.resultRgb[q*3+c]/=previewCounts[q];
        for(std::size_t g=0;g<groups;++g)preview.contributions[q*groups+g]/=previewCounts[q];
    }
    auto evidence=std::make_shared<RawImageData::MultiFrameMeasurementSidecars>();
    evidence->variance=variance;evidence->effectiveSupport=support;evidence->validity=validity;evidence->clipping=clipping;
    evidence->fusionUncertaintyVariance=uncertainty;
    evidence->localRejection=rejection;evidence->fallbackReason=fallback;
    evidence->originalFrameIds=native.multiFrameMeasurementSidecars->originalFrameIds;evidence->evidenceIdentitySha256=metadata.sourceContentSha256;
    raw->multiFrameMeasurementSidecars=evidence;
    auto analysis=std::make_shared<BracketingAnalysis>(*result.analysis);
    analysis->estimatedPeakResidentBytes=std::max(analysis->estimatedPeakResidentBytes,memoryPlan.peakBytes);
    analysis->diagnostics.push_back(memoryPlan.message);
    analysis->stageSeconds["super_resolution"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    analysis->diagnostics.push_back("Super-resolution "+std::to_string(scale)+"x / "+(gpu.used?"GPU verified against CPU":"CPU")+" / measured Bayer phase bins "+std::to_string(diversity)+" of 16.");
    if(!diverse)analysis->diagnostics.push_back("Limited sampling diversity. Noise reduction is supported; additional resolved detail is limited.");
    if(!gpu.fallback.empty())analysis->diagnostics.push_back("Super-resolution CPU fallback: "+gpu.fallback);
    result.analysis=analysis;result.raw=raw;result.preview=std::move(preview);result.reconstructionInspection=inspection;
    result.message="Super-resolution "+std::to_string(scale)+"x ready. "+std::to_string(width)+" x "+std::to_string(height)+". "+(!diverse?"Limited sampling diversity.":"Additional spatial cleanup is controlled in RAW.");
    return true;
}
}
