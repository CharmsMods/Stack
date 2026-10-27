#pragma once
#include "Raw/Bracketing/SuperResolution/Preparation.h"
#include "Raw/Bracketing/CaptureCorrespondence.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateSuperResolutionDepthSupport(const std::filesystem::path& directory);
// Requires the validation caller's current GL context. These small prepared-tile
// checks exercise production preparation and both the GPU and scalar verifier.
inline void ValidateSuperResolutionSupport(const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;using namespace Raw::Mfd;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    std::string error;
    auto data=std::make_shared<PreparedDataset>();data->directory=directory;
    data->width=data->height=64;data->sources.resize(2);data->origin=0;
    auto& source=data->sources[1];auto& frame=source.frame;
    frame.activeExtent={64,64};frame.sensorActiveArea={0,0,64,64};frame.activeCfaPattern=Raw::CfaPattern::RGGB;
    frame.tileRawPixels=64;frame.tileColumns=frame.tileRows=1;frame.cacheKey=std::string(64,'e');
    frame.calibration.usableSpanByCfaSite.fill(4095);
    source.noise.quality=NoiseModelQuality::TrustedMetadata;
    for(auto& n:source.noise.sites){n.offsetVariance=1e-4;n.quantizationIncluded=true;}
    PreparedRawTile input;input.extent={64,64};input.normalizedMosaic.assign(4096,.2f);
    input.comparisonGain.assign(4096,1.f);input.sampleFlags.assign(4096,0);
    DirectoryNormalizedTileCache cache(directory);
    if(!cache.Write(frame.cacheKey,input,&error))throw std::runtime_error(error);
    source.localMotion=std::make_shared<LocalMotionGrid>();auto& motion=*source.localMotion;
    motion.valid=true;motion.width=motion.height=2;motion.spacingRawX=motion.spacingRawY=63;
    motion.referenceRawExtent=motion.sourceRawExtent={64,64};motion.nodes.resize(4);
    for(auto& node:motion.nodes){node.state=MotionNodeState::Structured;node.confidence=1;node.covarianceRaw={.0025,0,.0025};}
    const auto reliability=[&](unsigned width,unsigned height,unsigned rejectX,unsigned rejectY) {
        ReliabilityMap map;map.valid=true;map.rawExtent={width,height};map.cellStrideBayerCells=2;
        map.cellExtent={(width/2+1)/2,(height/2+1)/2};map.cells.resize(map.cellExtent.width*map.cellExtent.height);
        for(auto& cell:map.cells)cell.reliability=1;
        if(rejectX<map.cellExtent.width&&rejectY<map.cellExtent.height)
            map.cells[rejectY*map.cellExtent.width+rejectX].rejectionBits=ReliabilityRejectMask(ReliabilityRejectBit::PatchRejected);
        source.reliability=std::make_shared<ReliabilityStore>();
        if(!BuildReliabilityStore(map,ReliabilityStorageFormat::Uint16Unorm,16,*source.reliability,&error))throw std::runtime_error(error);
        StampCaptureReliability(source);
    };
    BracketingAnalysis analysis;analysis.prepared=data;analysis.guideEv.assign(1024,0);
    PreparedTileSampler sampler(directory);Sr::FrameRegion region;
    const auto prepare=[&](const Sr::Tile& tile) {
        if(!Sr::BuildFrameRegion(analysis,source,tile,sampler,region,{},error))throw std::runtime_error(error);
    };
    const auto accumulate=[&](Sr::Tile tile,const Sr::FrameRegion& capture,bool gpu) {
        if(gpu) {
            Sr::GpuKernel kernel;
            const bool ok=kernel.Begin(tile,error)&&kernel.Add(tile,capture,error)&&kernel.Read(tile,error);
            kernel.Release();if(!ok)throw std::runtime_error(error);
        } else if(!Sr::AccumulateCpu(tile,capture,{},error))throw std::runtime_error(error);
        return tile;
    };
    const auto tileAt=[](float x,float y,unsigned width,unsigned height,float step) {
        Sr::Tile tile;tile.originX=x;tile.originY=y;tile.width=width;tile.height=height;tile.step=step;
        tile.pixels.resize(std::size_t(width)*height);for(auto& p:tile.pixels)p.kernel={1,0,1,.18f};return tile;
    };
    for(const unsigned scale:{1u,2u}) {
        const auto target=tileAt(scale==1?18.f:18.25f,scale==1?18.f:18.25f,scale==1?9u:17u,scale==1?9u:17u,1.f/scale);
        reliability(64,64,99,99);prepare(target);const auto cleanRegion=region;
        const auto cleanCpu=accumulate(target,region,false),cleanGpu=accumulate(target,region,true);
        reliability(64,64,5,5);prepare(target);
        check(region.supportRequired&&region.support.mode==CaptureSupportMask::Mode::Raster,"SR preparation omitted required support raster");
        check(region.support.width==3&&region.support.height==3&&region.support.cells.size()==9,"SR support crop or padded-tail fixture changed");
        for(std::size_t i=0;i<region.mapping.size();++i)
            check(region.mapping[i].z==cleanRegion.mapping[i].z,"Interior rejection unexpectedly changed coarse geometry confidence");
        const auto rejectedCpu=accumulate(target,region,false),rejectedGpu=accumulate(target,region,true);
        for(unsigned y=0;y<target.height;++y)for(unsigned x=0;x<target.width;++x) {
            const auto p=std::size_t(y)*target.width+x;
            const float rx=target.originX+x*target.step,ry=target.originY+y*target.step;
            const bool rejected=rx>=20&&rx<24&&ry>=20&&ry<24;
            check((EvaluateCaptureCorrespondence(source,{rx,ry}).confidence==0)==rejected,"Authoritative rejection fixture changed");
            for(unsigned c=0;c<3;++c) {
                const auto cw=rejectedCpu.pixels[p].broad[c].weight,gw=rejectedGpu.pixels[p].broad[c].weight;
                if(rejected)check(cw==0&&gw==0,"SR interpolated across an interior hard-rejected reliability cell");
                else check(cw==cleanCpu.pixels[p].broad[c].weight&&gw==cleanGpu.pixels[p].broad[c].weight&&gw>0,
                    "SR categorical mask changed neighboring valid support");
                check(std::abs(cw-gw)<2e-6f,"SR categorical support differs between CPU and GPU");
            }
        }
    }
    const auto target=tileAt(20,20,1,1,1);
    reliability(64,64,4,4);prepare(target); // Existing contributing coarse-corner veto remains.
    check(EvaluateCaptureCorrespondence(source,{20,20}).confidence>0,"Coarse-corner control changed");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight==0,"SR promoted an existing coarse-corner rejection");
    for(unsigned test=0;test<3;++test) {
        reliability(64,64,99,99);
        if(test==0)source.reliability.reset();
        if(test==1)++source.motionRevision;
        if(test==2)source.reliability->cellStrideBayerCells=0;
        prepare(target);
        check(region.support.mode==CaptureSupportMask::Mode::AllRejected,"Missing or stale evidence became optional SR support");
        for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight==0,"SR accepted unavailable required evidence");
    }
    reliability(64,64,99,99);prepare(target);const auto validRegion=region;
    for(unsigned test=0;test<7;++test) {
        auto malformed=validRegion;
        if(test==0)malformed.support={};
        if(test==1)malformed.support.cells.clear();
        if(test==2)malformed.support.cellStepRawPixels=0;
        if(test==3)malformed.support.originRawX=1;
        if(test==4)malformed.support.cells[0]=std::numeric_limits<float>::quiet_NaN();
        if(test==5)malformed.support.width=malformed.support.height=std::numeric_limits<std::uint32_t>::max();
        if(test==6)malformed.support.mode=static_cast<CaptureSupportMask::Mode>(255);
        check(!Sr::Validate(target,malformed,error),"Malformed required SR support raster bypassed validation");
        Sr::GpuKernel kernel;check(kernel.Begin(target,error),"SR validation GPU initialization failed");
        const bool accepted=kernel.Add(target,malformed,error);kernel.Release();check(!accepted,"GPU accepted malformed required SR support raster");
    }
    reliability(64,64,5,5);prepare(target);
    auto streamed=target;Sr::GpuKernel stream;
    const bool streamedOk=stream.Begin(streamed,error)&&stream.Add(streamed,validRegion,error)&&
        stream.Add(streamed,region,error)&&stream.Add(streamed,validRegion,error)&&stream.Read(streamed,error);
    stream.Release();check(streamedOk,"SR support streaming failed");
    const auto once=accumulate(target,validRegion,true);
    for(unsigned c=0;c<3;++c)check(std::abs(streamed.pixels[0].broad[c].weight-2*once.pixels[0].broad[c].weight)<2e-6f,
        "SR streamed support reused evidence from the preceding capture");
    // Reference/global mappings remain independent of local photometric masks.
    CaptureSupportMask mask;
    check(!BuildCaptureSupportMask(source,{std::numeric_limits<double>::quiet_NaN(),20},{20,20},false,mask,{},error),
        "Nonfinite SR support crop was accepted");
    check(BuildCaptureSupportMask(source,{20,20},{20,20},true,mask,{},error)&&mask.mode==CaptureSupportMask::Mode::NotRequired,"Reference unexpectedly requires support evidence");
    const auto local=source.localMotion;source.localMotion.reset();prepare(target);
    check(!region.supportRequired&&region.support.mode==CaptureSupportMask::Mode::NotRequired,"Global mapping unexpectedly requires support evidence");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight>0,"Global mapping lost valid support");
    source.localMotion=local;
    data->sources[0]=source;
    check(Sr::BuildFrameRegion(analysis,data->sources[0],target,sampler,region,{},error)&&
        !region.supportRequired&&region.support.mode==CaptureSupportMask::Mode::NotRequired,
        "Reference preparation unexpectedly required local support evidence");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight>0,"Reference mapping lost valid support");
    // Sparse/dark uncertainty is not contradictory categorical evidence.
    auto& sparse=source.reliability->tiles[0];sparse.uint16Values[5*16+5]=0;
    sparse.rejectionBits[5*16+5]=ReliabilityRejectMask(ReliabilityRejectBit::InsufficientPatchSupport);
    prepare(target);
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight>0,"SR turned uncertain texture into a hard rejection");
    // Reference extent deliberately differs from source extent; preserve the
    // short trailing native cells instead of stretching them or using the source.
    reliability(66,70,16,17);
    check(BuildCaptureSupportMask(source,{63.75,67.75},{65.75,69.75},false,mask,{},error),"Partial-cell support crop failed");
    check(mask.originRawX==60&&mask.originRawY==64&&mask.width==2&&mask.height==2&&mask.rawWidth==66&&mask.rawHeight==70&&
        mask.cells==std::vector<float>({1,1,1,0}),"Partial reliability cells changed reference-grid ownership");
    auto boundary=validRegion;boundary.support=mask;
    for(auto& m:boundary.mapping)m={20,20,1,0}; // Isolate mask lookup from physical source borders.
    for(const auto point:{RawCoordinate{63.75,69.75},RawCoordinate{64,69.75},RawCoordinate{65.75,69.75},RawCoordinate{66,69.75},RawCoordinate{-0.25,69.75}}) {
        const auto t=tileAt(float(point.x),float(point.y),1,1,.5f);
        for(bool gpu:{false,true})check((accumulate(t,boundary,gpu).pixels[0].broad[0].weight>0)==(point.x==63.75),"SR mask lookup stretched or escaped its reference domain");
    }
    reliability(64,64,99,99);unsigned calls=0;
    check(!Sr::BuildFrameRegion(analysis,source,target,sampler,region,[&]{return ++calls==2;},error)&&error=="Canceled",
        "SR support preparation ignored cancellation");
    prepare(target);check(Sr::Validate(target,region,error),"Canceled SR scratch could not be reused safely");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight>0,"Reused SR support retained canceled/rejected evidence");
    ValidateSuperResolutionDepthSupport(directory/"depth");
}

inline void ValidateSuperResolutionDepthSupport(const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;using namespace Raw::Mfd;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    std::string error;auto data=std::make_shared<PreparedDataset>();data->directory=directory;
    // One real prepared cache tile, not a generated full-width image. Global
    // coordinates preserve the production512-wide normalized depth boundary.
    data->width=4048;data->height=32;data->sources.resize(2);data->origin=0;
    auto& source=data->sources[1];auto& frame=source.frame;
    frame.activeExtent={4048,32};frame.sensorActiveArea={0,0,32,4048};frame.activeCfaPattern=Raw::CfaPattern::RGGB;
    frame.tileRawPixels=512;frame.tileColumns=8;frame.tileRows=1;frame.cacheKey=std::string(64,'d');
    frame.calibration.usableSpanByCfaSite.fill(4095);
    for(auto& n:source.noise.sites){n.offsetVariance=1e-4;n.quantizationIncluded=true;}
    source.noise.quality=NoiseModelQuality::TrustedMetadata;
    PreparedRawTile input;input.tileX=1;input.originX=512;input.extent={512,32};
    input.normalizedMosaic.assign(512*32,.2f);input.comparisonGain.assign(512*32,1);input.sampleFlags.assign(512*32,0);
    DirectoryNormalizedTileCache cache(directory);
    if(!cache.Write(frame.cacheKey,input,&error))throw std::runtime_error(error);
    BracketingAnalysis analysis;analysis.prepared=data;analysis.guideEv.assign(2024*16,0);
    auto depth=std::make_shared<Raw::DepthCorrespondenceSupport>();depth->width=512;depth->height=4;
    depth->rejected.assign(512*4,0);depth->rejected[2*512+85]=1;
    PreparedTileSampler sampler(directory);Sr::FrameRegion region;
    const auto tileAt=[](float x,unsigned width,float step) {
        Sr::Tile t;t.width=width;t.height=1;t.originX=x;t.originY=20;t.step=step;
        t.pixels.resize(width);for(auto& p:t.pixels)p.kernel={1,0,1,.18f};return t;
    };
    const auto prepare=[&](const Sr::Tile& t,const PreparedSource& capture) {
        if(!Sr::BuildFrameRegion(analysis,capture,t,sampler,region,{},error))throw std::runtime_error(error);
    };
    const auto accumulate=[&](Sr::Tile t,const Sr::FrameRegion& f,bool gpu) {
        if(gpu) {
            Sr::GpuKernel kernel;const bool ok=kernel.Begin(t,error)&&kernel.Add(t,f,error)&&kernel.Read(t,error);
            kernel.Release();if(!ok)throw std::runtime_error(error);
        } else if(!Sr::AccumulateCpu(t,f,{},error))throw std::runtime_error(error);
        return t;
    };
    for(unsigned local=0;local<2;++local) {
        if(local) {
            source.localMotion=std::make_shared<LocalMotionGrid>();auto& motion=*source.localMotion;
            motion.valid=true;motion.width=motion.height=2;motion.spacingRawX=4047;motion.spacingRawY=31;
            motion.referenceRawExtent=motion.sourceRawExtent={4048,32};motion.nodes.resize(4);
            for(auto& n:motion.nodes){n.state=MotionNodeState::Structured;n.confidence=1;n.covarianceRaw={.0025,0,.0025};}
            ReliabilityMap map;map.valid=true;map.rawExtent={4048,32};map.cellExtent={1012,8};map.cellStrideBayerCells=2;
            map.cells.resize(1012*8);for(auto& c:map.cells)c.reliability=1;
            source.reliability=std::make_shared<ReliabilityStore>();
            if(!BuildReliabilityStore(map,ReliabilityStorageFormat::Uint16Unorm,128,*source.reliability,&error))throw std::runtime_error(error);
            StampCaptureReliability(source);
        }
        for(unsigned scale:{1u,2u}) {
            const auto t=tileAt(scale==1?672.f:671.75f,scale==1?9u:18u,1.f/scale);
            source.depthSupport.reset();prepare(t,source);const auto clean=region;
            check(!region.depthSupportRequired&&region.depthSupport.values.empty(),"SR no-depth path performed dense support work");
            const auto cleanCpu=accumulate(t,clean,false),cleanGpu=accumulate(t,clean,true);
            source.depthSupport=depth;prepare(t,source);
            check(region.depthSupportRequired&&region.depthSupport.values.size()==t.pixels.size(),"SR omitted required per-output depth evidence");
            const auto cpu=accumulate(t,region,false),gpu=accumulate(t,region,true);
            for(unsigned x=0;x<t.width;++x) {
                const float rx=t.originX+x*t.step;
                const bool rejected=rx>=672.03125f&&rx<679.9375f;
                check(CaptureDepthRejects(source,{rx,20})==rejected,"Depth boundary control changed");
                check((EvaluateCaptureCorrespondence(source,{rx,20}).confidence==0)==rejected,"Depth owner and correspondence disagree");
                for(unsigned c=0;c<3;++c) {
                    const float cw=cpu.pixels[x].broad[c].weight,gw=gpu.pixels[x].broad[c].weight;
                    if(rejected)check(cw==0&&gw==0,"SR bypassed an interior depth-only categorical rejection");
                    else check(cw==cleanCpu.pixels[x].broad[c].weight&&gw==cleanGpu.pixels[x].broad[c].weight&&gw>0,
                        "SR depth veto changed neighboring valid support");
                    check(std::abs(cw-gw)<2e-6f,"SR depth veto differs between CPU and GPU");
                }
            }
        }
    }
    // Exact boundary and adjacent float control: lookup stays on the owner CPU,
    // rather than reconstructing this rational grid with GPU float arithmetic.
    for(float x:{std::nextafter(672.03125f,0.f),672.03125f,std::nextafter(679.9375f,0.f),679.9375f}) {
        const auto t=tileAt(x,1,.5f);prepare(t,source);const bool rejected=CaptureDepthRejects(source,{x,20});
        for(bool gpu:{false,true})check((accumulate(t,region,gpu).pixels[0].broad[0].weight==0)==rejected,
            "SR depth veto changed the owner's exact rational boundary");
    }
    const auto target=tileAt(676,1,1);prepare(target,source);const auto depthRegion=region;
    for(unsigned test=0;test<7;++test) {
        auto malformed=depthRegion;
        if(test==0)malformed.depthSupport.values.clear();
        if(test==1)malformed.depthSupport.originX+=1;
        if(test==2)malformed.depthSupport.step=.5f;
        if(test==3)malformed.depthSupport.width=2;
        if(test==4)malformed.depthSupport.values[0]=std::numeric_limits<float>::quiet_NaN();
        if(test==5)malformed.depthSupportRequired=false;
        if(test==6)malformed.depthSupport={};
        check(!Sr::Validate(target,malformed,error),"Malformed or mismatched required SR depth support bypassed validation");
        Sr::GpuKernel kernel;check(kernel.Begin(target,error),"SR depth validation GPU initialization failed");
        const bool ok=kernel.Add(target,malformed,error);kernel.Release();check(!ok,"GPU accepted malformed SR depth support");
    }
    data->sources[0]=source;prepare(target,data->sources[0]);
    check(!region.depthSupportRequired&&!CaptureDepthRejects(data->sources[0],{676,20},true),"Fixed reference acquired a depth veto");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight>0,"Fixed reference lost support to depth");
    // Preserve the pre-existing coarse-corner gate even when the exact output
    // lies in a different, allowed depth cell.
    depth->rejected[2*512+85]=0;depth->rejected[2*512+84]=1;prepare(target,source);
    check(!CaptureDepthRejects(source,{676,20}),"Depth coarse-corner control changed");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight==0,"SR promoted a coarse depth rejection");
    depth->rejected[2*512+84]=0;depth->rejected[2*512+85]=1;
    // Partial/unknown depth arrays retain exactly the existing default: only
    // present nonzero cells veto; absence is not affirmative depth evidence.
    auto partial=std::make_shared<Raw::DepthCorrespondenceSupport>(*depth);
    partial->rejected.resize(2*512+85);source.depthSupport=partial;prepare(target,source);
    check(!CaptureDepthRejects(source,{676,20}),"Missing depth cell became a rejection");
    for(bool gpu:{false,true})check(accumulate(target,region,gpu).pixels[0].broad[0].weight>0,"Unknown depth stopped valid correspondence");
    partial->rejected.clear();prepare(target,source);
    check(!region.depthSupportRequired&&region.depthSupport.values.empty(),"Empty depth evidence caused dense work");
    partial->rejected={1};partial->width=0;prepare(target,source);
    check(!region.depthSupportRequired&&!CaptureDepthRejects(source,{676,20}),"Unspecified depth dimensions became rejection evidence");
    source.depthSupport=depth;source.localMotion.reset();source.reliability.reset();unsigned calls=0;
    check(!Sr::BuildFrameRegion(analysis,source,target,sampler,region,[&]{return ++calls==2;},error)&&error=="Canceled",
        "SR depth-mask build ignored cancellation");
    source.depthSupport.reset();prepare(target,source);
    check(!region.depthSupportRequired&&region.depthSupport.values.empty(),"SR depth-to-none scratch retained a stale veto");
    auto streamed=target;Sr::GpuKernel kernel;
    const bool ok=kernel.Begin(streamed,error)&&kernel.Add(streamed,depthRegion,error)&&kernel.Add(streamed,region,error)&&kernel.Read(streamed,error);
    kernel.Release();check(ok,"SR depth-to-none streaming failed");
    const auto clean=accumulate(target,region,true);
    for(unsigned c=0;c<3;++c)check(streamed.pixels[0].broad[c].weight==clean.pixels[0].broad[c].weight,
        "SR GPU retained a depth veto after streaming a no-depth capture");
}
}
