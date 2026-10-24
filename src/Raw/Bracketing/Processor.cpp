#include "ProcessingInternal.h"
#include "AlignmentProposalInput.h"
#include "HdrColorFusion.h"
#include "PresentationEvidence.h"
#include "ProcessingTiles.h"
#include "TemporalGpu.h"
#include "SuperResolution/Reconstruction.h"
#include "Panorama/Panorama.h"
#include "Depth/Depth.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace Raw::Bracketing {
namespace {
std::uint64_t Hash(const std::string& s) {
    std::uint64_t h=14695981039346656037ull;
    for(unsigned char c:s) {h^=c;h*=1099511628211ull;}return h;
}
using ColorChannels=std::array<unsigned,4>;
ColorChannels PrepareColorChannels(const RawMetadata& metadata) {
    Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(metadata.cfaPattern,layout);
    ColorChannels channels{};
    for(unsigned i=0;i<4;++i) {
        const auto site=layout.SiteAt(i%2,i/2);
        channels[i]=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
    }
    return channels;
}
void StoreColor(float* rgb,const std::array<double,4>& values,const ColorChannels& channels) {
    for(unsigned c=0;c<3;++c) rgb[c]=0;
    for(unsigned i=0;i<4;++i) {
        const unsigned c=channels[i];
        rgb[c]+=static_cast<float>(values[i]*(c==1?.5:1));
    }
}

}



Preview ReblendPreview(const Preview& source,const BracketingRecipe& recipe,const BracketingAnalysis& analysis,
    const Raw::OpenGlTaskExecutor& executor,std::string* failure,bool includeSourceMeasurements,
    const std::function<bool()>& shouldCancel) {
    const auto n=recipe.groups.size(),pixels=static_cast<std::size_t>(source.width)*source.height;
    if(!n||n>64||pixels>std::numeric_limits<std::size_t>::max()/(4*n)||
        source.samples.size()!=pixels*4*n||source.guideEv.size()!=pixels) {
        if(failure)*failure="The retained bracket preview has incomplete source measurements.";
        return {};
    }
    if(shouldCancel&&shouldCancel()) {if(failure)*failure="Canceled";return {};}
    Preview out;
    if(includeSourceMeasurements)out=source;
    else {
        // Interactive reblends retain these immutable measurements in the
        // worker. Do not copy/recompute data that the client already owns.
        out.width=source.width;out.height=source.height;
        out.sensorOriginX=source.sensorOriginX;out.sensorOriginY=source.sensorOriginY;
        out.sensorStepX=source.sensorStepX;out.sensorStepY=source.sensorStepY;
        out.metadata=source.metadata;out.guideEv=source.guideEv;out.coverage=source.coverage;
    }
    out.resultRgb.assign(pixels*3,0);
    if(includeSourceMeasurements)out.sourceRgb.assign(pixels*3*n,0);
    out.contributions.assign(pixels*n,0);out.requested.assign(pixels*n,0);
    out.diagnostics.assign(pixels,0);
    const auto colorChannels=PrepareColorChannels(out.metadata);
    std::vector<std::array<double,4>> sources(includeSourceMeasurements?n:0);
    ColorObservations input;
    for(auto& site:input)site.resize(n);
    std::vector<double> shares;
    std::size_t nextCancellationRow=0;
    for(std::size_t p=0;p<pixels;++p) {
        if(shouldCancel&&p==nextCancellationRow) {
            nextCancellationRow+=source.width;
            if(shouldCancel()) {if(failure)*failure="Canceled";return {};}
        }
        Evaluate(recipe.automatic?analysis.suggestion:recipe.knots,out.guideEv[p],shares);
        std::array<double,4> cell{};
        for(unsigned c=0;c<4;++c) {
            for(std::size_t g=0;g<n;++g) {
                const auto& v=source.samples[(p*4+c)*n+g];
                input[c][g]={v.value,v.variance,v.headroom,v.support,v.fallback,v.exposure,
                    v.finite,v.clipped,v.localRejected,v.fixedReference,v.measurementVariance,v.uncertaintyVariance,v.fallbackClipped};
                if(includeSourceMeasurements)sources[g][c]=v.support>0?v.value:v.fallback;
            }
        }
        const auto color=BlendColor(input,shares,recipe);
        for(unsigned c=0;c<4;++c) {
            const auto& blended=color[c];cell[c]=blended.value;
            out.diagnostics[p]|=(blended.fallback?Preview::Fallback:0)|
                (blended.clipped?Preview::Unrecoverable:0)|
                (!blended.valid&&!blended.clipped?Preview::Invalid:0)|
                (blended.localRejected?Preview::AlignmentRejected:0)|
                (blended.fallbackReason==MeasurementFallbackReason::FixedReference?Preview::FixedReference:0)|
                (blended.fallbackReason==MeasurementFallbackReason::ShortestExposure?Preview::ShortestExposure:0);
            for(std::size_t g=0;g<n;++g) out.contributions[p*n+g]+=static_cast<float>(blended.actual[g]*.25);
        }
        StoreColor(out.resultRgb.data()+p*3,cell,colorChannels);
        for(std::size_t g=0;g<n;++g) {
            if(includeSourceMeasurements)StoreColor(out.sourceRgb.data()+(p*n+g)*3,sources[g],colorChannels);
            out.requested[p*n+g]=static_cast<float>(shares[g]);
        }
    }
    if(recipe.depth.enabled&&recipe.depth.blurRadius>0&&analysis.depth) {
        std::vector<float> blurred;std::string error;
        if(!Depth::RenderDefocusGpu(out.resultRgb,out.width,out.height,*analysis.depth,recipe.depth,
            out.sensorOriginX,out.sensorOriginY,out.sensorStepX,out.sensorStepY,blurred,executor,shouldCancel,error)) {
            if(failure)*failure=error;return {};
        }
        out.resultRgb=std::move(blurred);
    }
    return out;
}
BracketingResult Process(const ProcessingRequest& submittedRequest) {
    if(submittedRequest.recipe.reconstruction==ReconstructionMode::Panorama&&submittedRequest.alignmentProposals.empty()&&!submittedRequest.recipe.aiAlignment)return Panorama::Process(submittedRequest);
    ProcessingRequest request=submittedRequest;
    if(request.reportPresentation) {
        try {request.evidenceStream=std::make_shared<ProcessingEvidenceStream>();}
        catch(...) {request.reportPresentation={};request.evidenceStream.reset();}
    }
    if((request.recipe.reconstruction==ReconstructionMode::SuperResolution1x||
        request.recipe.reconstruction==ReconstructionMode::SuperResolution2x)&&submittedRequest.reportProgress)
        request.reportProgress=[report=submittedRequest.reportProgress](double p,const std::string& stage){report(.90*p,stage);};
    request.workerCount=ResolveBracketingWorkerCount(
        request.memoryBudgetBytes,request.workerCount);
    BracketingResult result;
    const auto fail=[&](const std::string& message) {
        result.message=message;result.status=BracketingResult::Status::Failed;
        // A failing cancellation callback must not throw again while an
        // exception from the processing workers is being reported.
        try {
            if(request.shouldCancel&&request.shouldCancel()) result.status=BracketingResult::Status::Canceled;
        } catch(...) {}
        return result;
    };
    try {
        std::string error;
        if(request.version!=1&&request.version!=2) return fail("Unsupported bracketing processing request version.");
        if(!Validate(request.recipe,error)) return fail(error);
        if(!request.executeOpenGlTask)
            return fail("Burst reconstruction requires an available OpenGL compute context; CPU substitution is not supported.");
        if(!request.preferGpuCombining)
            return fail("Burst reconstruction requires GPU capture combining; CPU mode is no longer supported.");
        if(request.recipe.alignmentMode==AlignmentMode::AutomaticLocal&&!request.preferGpuRegistration)
            return fail("Automatic local alignment requires GPU candidate search; CPU mode is no longer supported.");
        if(request.recipe.aiAlignment&&!request.alignmentProposals.empty())
            return fail("Choose generated AI alignment or external research proposals, not both.");
        if(!ValidateAlignmentProposals(request,error)) return fail(error);
        // Generated proposals are bounded to 884736 pixels per direction.
        // Reserve their pair even though generation occurs after calibration.
        std::uint64_t proposalBytes=request.recipe.aiAlignment?16ull*1024*1024:0;
        for(const auto& p:request.alignmentProposals)
            proposalBytes=std::max(proposalBytes,std::uint64_t(p.width)*p.height*16);
        // Keep proposer buffers inside the caller's budget, not on top of it.
        // The remaining preparation/merge policies continue to own their work.
        if(proposalBytes>=request.memoryBudgetBytes)return fail("Alignment proposals exceed the processing memory allowance.");
        request.memoryBudgetBytes-=proposalBytes;
        request.workerCount=ResolveBracketingWorkerCount(request.memoryBudgetBytes,request.workerCount);
        auto analysis=std::make_shared<BracketingAnalysis>();analysis->version=2;
        result.analysis=analysis;
        analysis->identity=AnalysisIdentity(request);
        const bool reusePrepared=request.analysis&&request.analysis->identity==analysis->identity&&request.analysis->prepared;
        const bool reuseGuide=reusePrepared;
        if(reusePrepared) *analysis=*request.analysis;
        else {
            auto prepared=std::make_shared<PreparedDataset>();
            prepared->combiningGpu=std::make_shared<TemporalGpu>(request);
            if(!prepared->combiningGpu->Ready(error))return fail(error);
            if(!Prepare(request,*prepared,analysis->diagnostics,error)) return fail(error);
            analysis->prepared=prepared;
            analysis->depth=prepared->depth;
            analysis->stageSeconds=prepared->stageSeconds;
            for(const auto& s:prepared->sources) {
                s.original->exposureScale=s.scale;
                analysis->originals[s.id]=s.original;
                analysis->relativeEv.push_back(-std::log2(s.scale));
                analysis->calibratedEv[s.id]=-std::log2(s.scale);
                analysis->noiseConfidence[s.id]=Mfd::NoiseModelQualityName(s.noise.quality);
                analysis->alignments[s.id]=s.alignmentDiagnostic;
                if(s.alignmentInspection) analysis->alignmentInspection[s.id]=s.alignmentInspection;
            }
        }
        const auto& data=*analysis->prepared;
        if(!data.combiningGpu||!data.combiningGpu->Ready(error))
            return fail(error.empty()?"Prepared Burst GPU backend is unavailable.":error);
        const auto& origin=data.sources[data.origin];
        auto guideMetadata=origin.metadata;guideMetadata.cfaPattern=origin.frame.activeCfaPattern;
        const auto guideLuminance=BuildLuminanceTransform(guideMetadata);
        const auto pixels=static_cast<std::size_t>(data.width)*data.height;
        const auto groups=request.recipe.groups.size();
        analysis->estimatedPeakResidentBytes=proposalBytes+EstimateBracketingPeakResidentBytes(data.width,data.height,
            data.sources.size(),groups,request.workerCount,
            request.recipe.alignmentMode==AlignmentMode::AutomaticLocal);
        const std::uint64_t tileBytes=static_cast<std::uint64_t>(origin.frame.tileRawPixels)*origin.frame.tileRawPixels*(groups*(sizeof(Observation)+2*sizeof(double))+80);
        if(pixels>std::numeric_limits<std::size_t>::max()/32 ||
           pixels*36ull+tileBytes+32ull*1024*1024>request.memoryBudgetBytes)
            return fail("The output and tile buffers exceed the available bracketing memory budget.");
        const unsigned cellWidth=data.width/2,cellHeight=data.height/2;
        if(!reuseGuide) {
            analysis->guideEv.clear();analysis->suggestion.clear();analysis->histogram.fill(0);
            analysis->diagnostics.erase(std::remove_if(analysis->diagnostics.begin(),analysis->diagnostics.end(),
                [](const std::string& message){return message.rfind("Temporal merge.",0)==0;}),analysis->diagnostics.end());
            const auto groupStart=std::chrono::steady_clock::now();
            TemporalGpu::Operation workspaceOperation(*data.combiningGpu);
            if(!ProcessTiles(request,data,.52,.33,"Combining exposure groups",ProcessingStage::Groups,
                [&](unsigned tx,unsigned ty,std::string& tileError) {
                    return PrepareGroupTile(request,data,tx,ty,tileError);
                },error)) return fail(error);
            if(!workspaceOperation.Finish(error))return fail(error);
            analysis->stageSeconds["temporal_groups"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-groupStart).count();
            {
                const auto& m=data.groupMetrics;std::lock_guard<std::mutex> lock(m.mutex);
                analysis->stageSeconds["group_temporal_worker_seconds"]=m.temporal;
                analysis->stageSeconds["group_local_detail_worker_seconds"]=m.detail;
                analysis->stageSeconds["group_cache_worker_seconds"]=m.cache;
                analysis->diagnostics.push_back("Group work (overlapping worker intervals): "+std::to_string(m.computedTiles)+
                    " computed tiles, "+std::to_string(m.cachedTiles)+" cache hits; local patches "+std::to_string(m.patches.attempted)+
                    ", center rejected "+std::to_string(m.patches.centerRejected)+", sample rejected "+std::to_string(m.patches.sampleRejected)+
                    ", expanded "+std::to_string(m.patches.expanded)+", accepted "+std::to_string(m.patches.accepted)+".");
            }
            if(data.combiningGpu)data.combiningGpu->Report(*analysis);
            const auto guideStart=std::chrono::steady_clock::now();
            if(std::count_if(data.sources.begin(),data.sources.end(),[](const auto& source){return source.enabled;})>1)
                analysis->diagnostics.push_back("Temporal merge. Local detail uses agreement across at least three captures. Additional spatial denoising is controlled in RAW.");
            analysis->guideEv.resize(static_cast<std::size_t>(cellWidth)*cellHeight);
            std::vector<std::vector<double>> histogram(256,std::vector<double>(groups));
            const auto tileCount=static_cast<std::size_t>(origin.frame.tileColumns)*origin.frame.tileRows;
            std::vector<std::array<float,256>> tileCounts(tileCount);
            std::vector<std::vector<double>> tileHistograms(
                tileCount,std::vector<double>(256*groups));
            if(!ProcessTiles(request,data,.85,.07,"Building neutral guide",ProcessingStage::Guide,
                [&](unsigned tx,unsigned ty,std::string& tileError) {
                std::vector<std::vector<Observation>> observations;Mfd::PreparedRawTile tile;
                if(!ReadGroups(request,data,tx,ty,observations,tile,tileError)) return false;
                const auto tileIndex=static_cast<std::size_t>(ty)*origin.frame.tileColumns+tx;
                auto& localCounts=tileCounts[tileIndex];
                auto& localHistogram=tileHistograms[tileIndex];
                ColorObservations colorInput;
                std::vector<double> weights(groups);
                for(unsigned y=0;y<tile.extent.height;y+=2) for(unsigned x=0;x<tile.extent.width;x+=2) {
                    std::array<double,4> cell{};
                    std::fill(weights.begin(),weights.end(),0.0);
                    GatherColorCell(observations,y*tile.extent.width+x,tile.extent.width,colorInput);
                    const auto color=BlendColor(colorInput,{},request.recipe);
                    for(unsigned c=0;c<4;++c) {
                        const auto& b=color[c];cell[c]=b.value;
                        for(std::size_t g=0;g<groups;++g) weights[g]+=b.actual[g]*.25;
                    }
                    const double ev=std::log2(std::max(1e-9,Luminance(guideLuminance,cell))/.18);
                    analysis->guideEv[((tile.originY+y)/2)*cellWidth+(tile.originX+x)/2]=static_cast<float>(ev);
                    const auto bin=static_cast<std::size_t>(std::clamp((ev+12)/20*255,0.0,255.0));
                    localCounts[bin]+=1;
                    for(std::size_t g=0;g<groups;++g) localHistogram[bin*groups+g]+=weights[g];
                }
                if(request.evidenceStream)request.evidenceStream->Tile(request,ProcessingStage::Guide,
                    tile.originX,tile.originY,unsigned(tile.extent.width),unsigned(tile.extent.height),[&](unsigned sx,unsigned sy) {
                    PresentationSample s;const auto ev=analysis->guideEv[(sy/2)*cellWidth+sx/2];
                    const double y=.18*std::exp2(ev);s.rgb={y,y,y};s.first=ev;return s;
                });
                return true;
            },error)) return fail(error);
            for(std::size_t tileIndex=0;tileIndex<tileCount;++tileIndex)
                for(std::size_t bin=0;bin<256;++bin) {
                    analysis->histogram[bin]+=tileCounts[tileIndex][bin];
                    for(std::size_t g=0;g<groups;++g)
                        histogram[bin][g]+=tileHistograms[tileIndex][bin*groups+g];
                }
            for(unsigned b=0;b<=16;++b) {
                const double ev=-12+b*1.25;const int center=static_cast<int>(b*255/16);
                std::vector<double> shares(groups);double sum=0;
                for(int delta=-16;delta<=16;++delta) {
                    const auto index=std::clamp(center+delta,0,255);
                    for(std::size_t g=0;g<groups;++g) shares[g]+=histogram[index][g];
                }
                for(auto v:shares) sum+=v;
                if(sum<=0) {
                    int nearest=-1;
                    for(int i=0;i<256;++i) if(analysis->histogram[i]>0&&(nearest<0||std::abs(i-center)<std::abs(nearest-center))) nearest=i;
                    if(nearest>=0) {shares=histogram[nearest];for(auto v:shares) sum+=v;}
                }
                for(auto& v:shares) v=sum>0?v/sum:1.0/groups;
                analysis->suggestion.push_back({ev,ev-(b?1.25/3:0),ev+(b<16?1.25/3:0),shares,shares,shares});
            }
            analysis->stageSeconds["neutral_guide"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-guideStart).count();
        }
        if(request.preparationOnly) {
            result.status=BracketingResult::Status::Completed;
            result.message="Bracket inspection ready.";
            return result;
        }
        const auto mergeStart=std::chrono::steady_clock::now();
        auto mosaic=std::make_shared<std::vector<float>>(pixels);
        auto variance=std::make_shared<std::vector<float>>(pixels);
        auto uncertainty=std::make_shared<std::vector<float>>(pixels);
        auto support=std::make_shared<std::vector<float>>(pixels);
        auto validity=std::make_shared<std::vector<std::uint8_t>>(pixels);
        auto clipping=std::make_shared<std::vector<std::uint8_t>>(pixels);
        auto localRejection=std::make_shared<std::vector<std::uint8_t>>(pixels);
        auto fallbackReason=std::make_shared<std::vector<std::uint8_t>>(pixels);
        Preview preview;preview.metadata=origin.metadata;preview.metadata.cfaPattern=origin.frame.activeCfaPattern;
        const auto previewColorChannels=PrepareColorChannels(preview.metadata);
        const double maxCells=std::min(640.0*640,16.0*1024*1024/(4*groups*sizeof(Preview::Sample)+16*groups));
        const unsigned stride=std::max(1u,static_cast<unsigned>(std::ceil(std::sqrt(static_cast<double>(cellWidth)*cellHeight/maxCells))));
        preview.width=(cellWidth+stride-1)/stride;preview.height=(cellHeight+stride-1)/stride;
        preview.sensorStepX=preview.sensorStepY=2.0*stride;
        preview.samples.resize(static_cast<std::size_t>(preview.width)*preview.height*4*groups);
        preview.guideEv.resize(static_cast<std::size_t>(preview.width)*preview.height);
        if(request.evidenceStream) {
            preview.contributions.resize(std::size_t(preview.width)*preview.height*groups);
            preview.requested.resize(preview.contributions.size());
        }
        std::atomic<std::uint64_t> fallbackSamples{0},unrecoverableSamples{0},invalidSamples{0};
        if(!ProcessTiles(request,data,.92,.08,"Blending bracket",ProcessingStage::Blend,
            [&](unsigned tx,unsigned ty,std::string& tileError) {
            std::vector<std::vector<Observation>> observations;Mfd::PreparedRawTile tile;
            if(!ReadGroups(request,data,tx,ty,observations,tile,tileError)) return false;
            std::uint64_t tileFallback=0,tileUnrecoverable=0,tileInvalid=0;
            ColorObservations colorInput;
            std::vector<double> shares;
            for(unsigned y=0;y<tile.extent.height;y+=2) for(unsigned x=0;x<tile.extent.width;x+=2) {
                const auto cx=(tile.originX+x)/2,cy=(tile.originY+y)/2;
                const double ev=analysis->guideEv[cy*cellWidth+cx];
                Evaluate(request.recipe.automatic?analysis->suggestion:request.recipe.knots,ev,shares);
                const bool keep=cx%stride==0&&cy%stride==0;
                const auto previewIndex=(cy/stride)*preview.width+cx/stride;
                if(keep) preview.guideEv[previewIndex]=static_cast<float>(ev);
                GatherColorCell(observations,y*tile.extent.width+x,tile.extent.width,colorInput);
                const auto color=BlendColor(colorInput,shares,request.recipe);
                for(unsigned c=0;c<4;++c) {
                    const auto p=(y+c/2)*tile.extent.width+x+c%2;
                    for(std::size_t g=0;g<groups;++g) {
                        const auto& o=colorInput[c][g];
                        if(keep) preview.samples[(previewIndex*4+c)*groups+g]={static_cast<float>(o.value),static_cast<float>(o.variance),
                            static_cast<float>(o.headroom),static_cast<float>(o.support),static_cast<float>(o.fallback),static_cast<float>(o.exposure),
                            o.finite,o.clipped,o.localRejected,o.fixedReference,
                            static_cast<float>(o.measurementVariance),static_cast<float>(o.uncertaintyVariance),o.fallbackClipped};
                    }
                    const auto& b=color[c];
                    if(keep&&request.evidenceStream)for(std::size_t g=0;g<groups;++g) {
                        preview.contributions[previewIndex*groups+g]+=float(b.actual[g]*.25);
                        preview.requested[previewIndex*groups+g]=float(shares[g]);
                    }
                    const auto index=(tile.originY+y+c/2)*data.width+tile.originX+x+c%2;
                    const double gain=tile.comparisonGain[p];
                    (*mosaic)[index]=static_cast<float>(b.value/gain);(*variance)[index]=static_cast<float>(b.measurementVariance/(gain*gain));
                    (*uncertainty)[index]=static_cast<float>(b.uncertaintyVariance/(gain*gain));
                    (*support)[index]=static_cast<float>(b.support);(*validity)[index]=b.valid;(*clipping)[index]=b.clipped;
                    (*localRejection)[index]=b.localRejected;
                    (*fallbackReason)[index]=static_cast<std::uint8_t>(b.fallbackReason);
                    tileFallback+=b.fallback;tileUnrecoverable+=b.clipped;tileInvalid+=!b.valid&&!b.clipped;
                }
            }
            fallbackSamples.fetch_add(tileFallback,std::memory_order_relaxed);
            unrecoverableSamples.fetch_add(tileUnrecoverable,std::memory_order_relaxed);
            invalidSamples.fetch_add(tileInvalid,std::memory_order_relaxed);
            if(request.evidenceStream)request.evidenceStream->Tile(request,ProcessingStage::Blend,
                tile.originX,tile.originY,unsigned(tile.extent.width),unsigned(tile.extent.height),[&](unsigned sx,unsigned sy) {
                PresentationSample s;
                // Select the nearest retained cell inside this completed tile.
                // Neighboring tiles may still belong to another worker.
                const unsigned step=stride*2;
                const auto minX=(tile.originX+step-1)/step,minY=(tile.originY+step-1)/step;
                const auto maxX=(tile.originX+unsigned(tile.extent.width)-2)/step,maxY=(tile.originY+unsigned(tile.extent.height)-2)/step;
                if(minX>maxX||minY>maxY){s.known=false;return s;}
                const auto px=std::clamp<unsigned>((sx+step/2)/step,unsigned(minX),unsigned(maxX));
                const auto py=std::clamp<unsigned>((sy+step/2)/step,unsigned(minY),unsigned(maxY));
                const unsigned rx=px*stride*2,ry=py*stride*2;
                if(rx<tile.originX||ry<tile.originY||rx+1>=tile.originX+tile.extent.width||ry+1>=tile.originY+tile.extent.height){s.known=false;return s;}
                std::array<double,4> cell{};
                for(unsigned c=0;c<4;++c) {
                    const auto p=(ry-tile.originY+c/2)*tile.extent.width+rx-tile.originX+c%2;
                    cell[c]=(*mosaic)[std::size_t(ry+c/2)*data.width+rx+c%2]*tile.comparisonGain[p];
                }
                std::array<float,3> rgb{};StoreColor(rgb.data(),cell,previewColorChannels);
                s.rgb={rgb[0],rgb[1],rgb[2]};
                const auto q=(std::size_t(py)*preview.width+px)*groups;
                const auto g=PresentationGroup(request);
                s.first=preview.contributions[q+g];s.second=preview.requested[q+g];return s;
            });
            return true;
        },error)) return fail(error);
        result.fallbackSamples=fallbackSamples.load(std::memory_order_relaxed);
        result.unrecoverableSamples=unrecoverableSamples.load(std::memory_order_relaxed);
        result.invalidSamples=invalidSamples.load(std::memory_order_relaxed);
        result.identity=Stack::NodeMath::Sha256ContentIdentity(analysis->identity+Identity(request.recipe)+data.motionIdentity+data.depthIdentity);
        auto raw=std::make_shared<RawImageData>();raw->metadata=origin.metadata;
        raw->metadata.sourcePath="bracketing://result";raw->metadata.sourceContentSha256=result.identity.substr(7);
        raw->metadata.sourceByteSize=pixels*sizeof(float);
        raw->metadata.rawWidth=raw->metadata.visibleWidth=data.width;raw->metadata.rawHeight=raw->metadata.visibleHeight=data.height;
        raw->metadata.leftMargin=raw->metadata.topMargin=0;raw->metadata.cfaPattern=origin.frame.activeCfaPattern;
        raw->metadata.dngActiveArea={0,0,static_cast<int>(data.height),static_cast<int>(data.width)};
        raw->metadata.hasDngActiveArea=true;raw->metadata.dngMaskedAreas.clear();
        raw->metadata.hasDngBaselineExposure=false;raw->metadata.dngBaselineExposure=0;
        raw->normalizedMosaicBuffer=mosaic;raw->normalizedMosaicContentHash=Hash(result.identity);
        raw->normalizedMosaicInputContract=NormalizedMosaicInputContract::BracketingPreGain;
        raw->contentIdentity=result.identity;raw->contentIdentityHash=raw->normalizedMosaicContentHash;
        auto sidecars=std::make_shared<RawImageData::MultiFrameMeasurementSidecars>();
        sidecars->variance=variance;sidecars->effectiveSupport=support;sidecars->validity=validity;sidecars->clipping=clipping;
        sidecars->fusionUncertaintyVariance=uncertainty;
        sidecars->localRejection=localRejection;sidecars->fallbackReason=fallbackReason;
        for(const auto& s:data.sources) if(s.enabled) sidecars->originalFrameIds.push_back(s.id);
        sidecars->evidenceIdentitySha256=raw->metadata.sourceContentSha256;raw->multiFrameMeasurementSidecars=sidecars;
        result.raw=raw;result.analysis=analysis;result.preview=ReblendPreview(preview,request.recipe,*analysis,request.executeOpenGlTask,&error,true,request.shouldCancel);
        if(!result.preview.width)return fail(error);
        // Reblend owns its copy. Release the construction cells before SR's
        // much larger output is allocated.
        preview={};
        result.status=BracketingResult::Status::Completed;
        analysis->stageSeconds["hdr_output_and_preview"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-mergeStart).count();
        const auto enabled=std::count_if(data.sources.begin(),data.sources.end(),[](const auto& s){return s.enabled;});
        result.message=enabled==1?"Single capture contribution. No multi-frame noise reduction or additional recovered dynamic range.":"Bracket ready.";
        auto reconstructionRequest=request;
        reconstructionRequest.reportProgress=submittedRequest.reportProgress;
        if(!ReconstructSuperResolution(reconstructionRequest,result,error)) return fail(error);
        if(request.recipe.depth.enabled) {
            ProcessingTimer timer(analysis->stageSeconds,"depth_output");
            if(!Depth::Finish(reconstructionRequest,result,error)) return fail(error);
        }
        return result;
    } catch(const std::exception& e) {return fail(std::string("Bracketing failed: ")+e.what());}
    catch(...) {return fail("Bracketing failed with an unknown processing error.");}
}
} // namespace Raw::Bracketing
