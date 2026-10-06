#pragma once
#include "Raw/Bracketing/SuperResolution/Preparation.h"
#include "Raw/Bracketing/SuperResolution/Reconstruction.h"
#include "Raw/Bracketing/SuperResolution/Memory.h"
#include "Renderer/RenderPipeline.h"
#include <fstream>
#include <random>

namespace Stack::Validation::SrContracts {
using namespace Raw::Bracketing;
inline void Check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
inline void MemoryPlanning() {
    constexpr std::uint64_t MiB=1024ull*1024,GiB=1024*MiB;
    Sr::MemoryRequirements r;
    r.residentBytes=384*MiB;r.outputBytes=1536*MiB;r.workingBytes=200*MiB;
    r.sourceTileBytes=3*MiB;r.preferredCacheTiles=40;r.bytesPerOutputTilePixel=640;
    Raw::Mfd::PhysicalMemorySnapshot memory;
    memory.valid=true;memory.totalPhysicalBytes=16*GiB;memory.availablePhysicalBytes=2816*MiB;
    memory.commitLimitKnown=true;memory.availableCommitBytes=5*GiB;
    const auto old=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,memory);
    Check(!Sr::PlanMemory(r,old.budgetBytes,false,memory).fits,"Strict SR budget was ignored");
    const auto automatic=Sr::PlanMemory(r,old.budgetBytes,true,memory);
    Check(automatic.fits&&automatic.additionalBytes<=automatic.additionalBudgetBytes&&
        automatic.peakBytes==r.residentBytes+automatic.additionalBytes,"SR double-counted resident preparation against free RAM");
    const auto tight=Sr::PlanMemory(r,r.residentBytes+r.outputBytes+r.workingBytes+4*r.sourceTileBytes+4*MiB,false,memory);
    Check(tight.fits&&tight.tileSide<256&&tight.peakBytes<=tight.budgetBytes,"SR did not shrink working tiles to fit");
    memory.availableCommitBytes=600*MiB;
    Check(!Sr::PlanMemory(r,10*GiB,true,memory).fits,"SR ignored system commit pressure");
    memory.availableCommitBytes=5*GiB;memory.availablePhysicalBytes=400*MiB;
    const auto low=Sr::PlanMemory(r,10*GiB,true,memory);
    Check(!low.fits&&low.message.find("GiB")!=std::string::npos,"SR ignored physical pressure or omitted size diagnostics");
    memory.totalPhysicalBytes=64*GiB;memory.availablePhysicalBytes=32*GiB;memory.availableCommitBytes=40*GiB;
    r.outputBytes=12*GiB;
    Check(Sr::PlanMemory(r,GiB,true,memory).fits,"SR retained a fixed cap on a larger machine");
    memory.valid=false;
    Check(!Sr::PlanMemory(r,GiB,true,memory).fits,"Unknown memory bypassed the caller's budget");
}
inline Raw::RawImageData Capture(unsigned index,double exposure) {
    Raw::RawImageData raw;auto& m=raw.metadata;
    m.sourceContentSha256=std::string(64,char('a'+index));m.sourceByteSize=128*96*2;m.sourcePath=std::to_string(index);
    m.cameraMake="Stack";m.cameraModel="SR fixture";m.dngUniqueCameraModel=m.cameraModel;
    m.rawWidth=m.visibleWidth=128;m.rawHeight=m.visibleHeight=96;m.orientation=1;
    m.bitDepth=16;m.cfaPattern=Raw::CfaPattern::RGGB;m.pixelLayout=Raw::RawPixelLayout::MosaicBayer;m.mosaiced=true;m.isDng=true;
    m.blackLevel=2048;m.perChannelBlack.fill(2048);m.whiteLevel=62000;
    m.exposureTimeSeconds=float(.01*exposure);m.isoSpeed=100;m.apertureFNumber=4;
    m.hasExposureTime=m.hasIsoSpeed=m.hasApertureFNumber=true;
    m.cameraWhiteBalance={1,1,1,1};m.cameraToSrgb={1,0,0,0,1,0,0,0,1};
    m.hasDngNoiseProfile=true;m.dngNoiseProfile={{0,1e-6},{0,1e-6},{0,1e-6}};
    raw.rawBuffer.resize(128*96);std::mt19937 rng(index+19);std::normal_distribution<double> noise(0,.001);
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x) {
        const unsigned color=((y&1)<<1)|(x&1);
        const double base=x<32?-.01:x<64?.2+.03*color:x<96?1.2:5.0;
        raw.rawBuffer[y*128+x]=std::uint16_t(std::clamp(std::llround(2048+59952*(base*exposure+noise(rng))),0ll,62000ll));
    }
    return raw;
}
inline ProcessingRequest Request(const std::filesystem::path& root) {
    std::vector<Raw::RawImageData> images{Capture(0,1),Capture(1,.25)};
    ProcessingRequest r;r.cacheDirectory=root;r.memoryBudgetBytes=1024ull*1024*1024;r.workerCount=2;
    r.recipe.originFrameId="f0";r.recipe.alignmentMode=AlignmentMode::FixedCoordinates;r.recipe.automatic=false;
    for(unsigned i=0;i<images.size();++i) {
        const auto id="f"+std::to_string(i);
        r.sources.push_back({id,images[i].metadata.sourceContentSha256,images[i].metadata.sourceByteSize,std::to_string(i)});
        r.recipe.groups.push_back({"g"+std::to_string(i),id,true,{{id,true,true,i?-2.:0.}}});
    }
    r.recipe.knots=EqualCurves(2);
    r.decode=[images](const std::filesystem::path& path,Raw::RawImageData& raw,const auto&){raw=images.at(std::stoul(path.string()));return true;};
    return r;
}
inline double Difference(const std::vector<float>& a,const std::vector<float>& b) {
    Check(a.size()==b.size(),"RAW render dimensions changed unexpectedly");double largest=0;
    for(std::size_t i=0;i<a.size();++i){Check(std::isfinite(b[i]),"Nonfinite RAW output");largest=std::max(largest,double(std::abs(a[i]-b[i])));}
    return largest;
}
inline void RawHandoff(const std::shared_ptr<const Raw::RawImageData>& raw,nlohmann::json& report) {
    RenderPipeline pipeline;pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetRawDevelopmentViewportValidationEnabled(false);
    pipeline.SetPreviewMaxDimension(0);
    RenderGraphSnapshot graph;RenderGraphNode node;node.nodeId=1;node.kind=RenderGraphNodeKind::RawDevelopment;
    node.rawDevelopment.embeddedRawData=raw;
    auto recipe=RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath,"Super-resolution RAW fixture");
    recipe.source.fingerprint=raw->metadata.sourceContentSha256;
    recipe.rgbDenoise.enabled=false;recipe.technical.mosaicDenoise.enabled=false;
    recipe.finishTone.layerJson["enabled"]=false;recipe.viewTransform.layerJson["enabled"]=false;
    node.rawDevelopment.recipe=recipe;graph.nodes.push_back(node);
    RenderGraphNode target;target.nodeId=2;target.kind=RenderGraphNodeKind::Output;graph.nodes.push_back(target);
    graph.links.push_back({1,"imageOut",2,"imageIn"});graph.outputNodeId=2;graph.outputSocketId="imageOut";
    const auto render=[&] {
        pipeline.ExecuteGraph(graph);const int w=pipeline.GetCanvasWidth(),h=pipeline.GetCanvasHeight();
        Check(pipeline.GetOutputTexture()&&w>0&&h>0,"SR RAW render failed");
        std::vector<float> pixels(std::size_t(w)*h*4);glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());return pixels;
    };
    const auto baseline=render();
    Check(pipeline.GetCanvasWidth()==raw->metadata.visibleWidth&&pipeline.GetCanvasHeight()==raw->metadata.visibleHeight,"RAW reduced SR dimensions");
    auto& edit=graph.nodes[0].rawDevelopment.recipe;
    edit.technical.demosaicMethod=Raw::DemosaicMethod::NearestNeighbor;edit.technical.mosaicDenoise.enabled=true;
    Check(Difference(baseline,render())<.002,"RAW applied CFA operations to reconstructed RGB");
    edit=recipe;edit.preToneExposureEv=1;
    report["exposureDifference"]=Difference(baseline,render());
    Check(report["exposureDifference"].get<double>()>.02,"Exposure edit did not reach SR RAW output");
    edit=recipe;edit.rgbDenoise.enabled=true;edit.rgbDenoise.lumaMap.baseMultiplier=.7f;edit.rgbDenoise.chromaMap.baseMultiplier=.7f;
    report["rgbDenoiseDifference"]=Difference(baseline,render());
    Check(report["rgbDenoiseDifference"].get<double>()>1e-6,"RGB denoise did not reach SR output");
    edit=recipe;Check(Difference(baseline,render())<.002,"Disabling RGB denoise did not restore SR");
    const int w=raw->metadata.visibleWidth,h=raw->metadata.visibleHeight;
    Raw::ViewportRegion region{w,h,40,30,96,80};pipeline.SetRawViewportRequest({region,1.,1});
    const auto cropped=render();const auto actual=pipeline.GetRawViewportRegion();
    Check(actual.Valid()&&pipeline.GetCanvasWidth()==actual.width,"SR native-region request failed");
    double delta=0;
    for(int y=0;y<actual.height;++y)for(int x=0;x<actual.width;++x)for(int c=0;c<3;++c) {
        const auto full=((h-actual.y-actual.height+y)*w+actual.x+x)*4+c;
        delta=std::max(delta,double(std::abs(cropped[(y*actual.width+x)*4+c]-baseline[full])));
    }
    report["roiDifference"]=delta;Check(delta<.004,"SR native region differs from full RAW render");
    pipeline.Shutdown();
}
inline void Validate(const std::filesystem::path& root,nlohmann::json& report) {
    MemoryPlanning();report["memoryPlanningPassed"]=true;
    auto request=Request(root/"synthetic-prepared");std::string error;
    for(auto mode:{ReconstructionMode::Standard,
        ReconstructionMode::SuperResolution1x,ReconstructionMode::SuperResolution2x}) {
        request.recipe.reconstruction=mode;BracketingRecipe restored;
        Check(Deserialize(Serialize(request.recipe),restored,error)&&Identity(restored)==Identity(request.recipe),"SR recipe round trip");
    }
    auto old=Serialize(request.recipe);old["version"]=3;
    BracketingRecipe legacy;Check(Deserialize(old,legacy,error)&&legacy.reconstruction==ReconstructionMode::Standard,"Legacy project changed reconstruction");
    auto result=Process(request);Check(result.status==BracketingResult::Status::Completed,result.message);
    const auto whole=Sr::MakeTile(0,0,32,24,2,*result.analysis,true),part=Sr::MakeTile(7,5,17,13,2,*result.analysis,true);
    for(unsigned y=0;y<part.height;++y)for(unsigned x=0;x<part.width;++x) {
        const auto a=whole.pixels[(y+5)*whole.width+x+7].kernel,b=part.pixels[y*part.width+x].kernel;
        Check(std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)+std::abs(a.w-b.w)<1e-6f,"SR reconstruction kernel changes at tile boundaries");
    }
    const auto& raw=*result.raw;Check(raw.reconstructedCameraRgb&&raw.metadata.visibleWidth==256&&raw.metadata.visibleHeight==192,"SR RGB contract");
    const auto pixel=[&](unsigned x,unsigned y,unsigned c){return raw.linearFloatBuffer[(y*256+x)*3+c];};
    Check(pixel(24,80,0)<-.008f,"SR clipped negative measurements");
    Check(std::abs(pixel(160,80,0)-1.2f)<.01f,"SR lost exposure-normalized overrange detail");
    const auto allClipped=80*256+224;
    Check((*raw.multiFrameMeasurementSidecars->clipping)[allClipped]&&!(*raw.multiFrameMeasurementSidecars->validity)[allClipped],"SR fabricated all-clipped recovery");
    Check((*raw.multiFrameMeasurementSidecars->fallbackReason)[allClipped]==std::uint8_t(MeasurementFallbackReason::ShortestExposure),"SR omitted shortest-exposure fallback");
    for(float value:raw.linearFloatBuffer)Check(std::isfinite(value),"SR produced NaN");
    auto detail=SuperResolutionDetail(result,128,96,100);
    for(unsigned y=0;y<detail.height;++y)for(unsigned x=0;x<detail.width;++x) {
        double sum=0;for(unsigned g=0;g<2;++g)sum+=detail.contributions[(y*detail.width+x)*2+g];
        Check(std::abs(sum-1)<1e-5,"SR actual contribution mask does not sum to one");
        const unsigned sx=unsigned(std::lround((detail.sensorOriginX+.5)*2-.5))+x,sy=unsigned(std::lround((detail.sensorOriginY+.5)*2-.5))+y;
        for(unsigned c=0;c<3;++c)Check(detail.resultRgb[(y*detail.width+x)*3+c]==pixel(sx,sy,c),"SR preview/full pixel mismatch");
    }
    request.analysis=result.analysis;request.decode=[](const auto&,auto&,const auto&)->bool{throw std::runtime_error("Unexpected re-decode");};
    request.executeOpenGlTask=[](Raw::OpenGlTask,std::string& e){e="Simulated unavailable graphics device";return false;};
    auto fallback=Process(request);Check(fallback.status==BracketingResult::Status::Completed,fallback.message);
    Check(Difference(raw.linearFloatBuffer,fallback.raw->linearFloatBuffer)<1e-7,"GPU failure changed CPU fallback output");
    request.recipe.reconstruction=ReconstructionMode::SuperResolution1x;
    auto nativeSize=Process(request);
    Check(nativeSize.status==BracketingResult::Status::Completed&&nativeSize.raw->reconstructedCameraRgb&&
        nativeSize.raw->metadata.visibleWidth==128&&nativeSize.raw->metadata.visibleHeight==96&&
        nativeSize.analysis->prepared==result.analysis->prepared,"SR 1x changed dimensions or discarded preparation");
    request.recipe.reconstruction=ReconstructionMode::SuperResolution2x;
    request.recipe.knots.front().share={.9,.1};request.recipe.knots.front().right={.9,.1};
    auto edited=Process(request);Check(edited.status==BracketingResult::Status::Completed&&edited.analysis->prepared==result.analysis->prepared,"Curve edit did not reuse SR preparation");
    request.memoryBudgetBytes=1024;auto failed=Process(request);Check(failed.status==BracketingResult::Status::Failed,"SR low-memory request was accepted");
    request.memoryBudgetBytes=1024ull*1024*1024;request.shouldCancel=[]{return true;};
    Check(Process(request).status==BracketingResult::Status::Canceled,"SR cancellation failed");
    RawHandoff(result.raw,report["rawHandoff"]);
    {
        const auto path=result.reconstructionInspection->directory/"0-0.bin";
        std::fstream corrupt(path,std::ios::binary|std::ios::in|std::ios::out);char byte=0;
        corrupt.read(&byte,1);byte^=1;corrupt.seekp(0);corrupt.write(&byte,1);corrupt.close();
        bool rejected=false;try {SuperResolutionDetail(result,64,64,48);}catch(const std::exception&) {rejected=true;}
        Check(rejected,"Damaged SR inspection cache was accepted");
    }
    report["contractsPassed"]=true;
}
}
