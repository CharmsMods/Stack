#include "Depth.h"
#include "GpuGeometry.h"
#include "GeometryMathShader.h"
#include "FusionShaders.h"
#include "CompletionShader.h"
#include "../ProcessingInternal.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>

namespace Raw::Bracketing::Depth {
bool Estimate(const ProcessingRequest& request,PreparedDataset& data,std::vector<std::string>& diagnostics,std::string& error){
    if(!request.recipe.depth.enabled)return true;
    ProcessingTimer timer(data.stageSeconds,"depth_geometry");data.depthIdentity="gpu-depth-unavailable-v1";
    auto map=std::make_shared<ReconstructionDepth>();data.depth=map;
    map->sensorWidth=data.width;map->sensorHeight=data.height;
    const auto unavailable=[&](const std::string& reason){map->message="Depth unavailable: "+reason;
        diagnostics.push_back(map->message);return true;};
    if(!request.executeOpenGlTask)return unavailable("an OpenGL compute context is required.");
    // Includes nine guide pyramids, tracking, evidence and shader workspaces.
    if(request.memoryBudgetBytes<256ull*1024*1024)return unavailable("the GPU tracking workspace exceeds the processing budget.");
    diagnostics.push_back("GPU depth: area-filtered RAW guides, normalized pyramidal feature tracking and image-only camera geometry. No lens or motion metadata.");
    GpuRuntime gpu(request);GpuGuide reference;
    if(!BuildGuide(gpu,data,data.origin,reference,error))return false;
    const auto longest=std::max(reference.width,reference.height);
    map->width=std::max(1u,unsigned(std::uint64_t(512)*reference.width/longest));
    map->height=std::max(1u,unsigned(std::uint64_t(512)*reference.height/longest));
    const unsigned count=map->width*map->height;
    GLuint evidence=0,evidenceProgram=0;
    if(!gpu.Run([&](std::string& e){
        evidence=gpu.Buffer(std::size_t(count)*8*2*sizeof(float));
        const auto code=std::string("#version 430 core\n")+Shaders::GeometryMath+Shaders::DepthEvidence;
        evidenceProgram=gpu.Program(code.c_str(),e);return evidenceProgram!=0;
    },error))return false;
    std::vector<std::size_t> sources;
    for(std::size_t i=0;i<data.sources.size();++i)if(i!=data.origin&&data.sources[i].enabled)sources.push_back(i);
    unsigned accepted=0;std::vector<std::size_t> acceptedSources;const auto views=std::min<std::size_t>(8,sources.size());
    for(std::size_t view=0;view<views;++view){
        const auto index=sources[views==1?0:view*(sources.size()-1)/(views-1)];
        if(request.reportProgress)request.reportProgress(.515,"GPU depth feature tracking "+std::to_string(view+1)+"/"+std::to_string(views));
        GpuGuide alternate;GpuTracks sparse;GpuPose pose;
        if(!BuildGuide(gpu,data,index,alternate,error)||!TrackFeatures(gpu,reference,alternate,sparse,false,error)||
           !FitGeometry(gpu,reference,sparse,pose,error))return false;
        diagnostics.push_back(data.sources[index].id+": GPU matches "+std::to_string(int(pose.counts[3]))+
            ", epipolar inliers "+std::to_string(int(pose.counts[0]))+", planar inliers "+std::to_string(int(pose.counts[2]))+
            ", geometry "+(pose.accepted?"accepted":"unresolved")+", ambiguity "+std::to_string(int(pose.status[2]))+".");
        if(!pose.accepted)continue;
        GpuTracks dense;
        if(!TrackFeatures(gpu,reference,alternate,dense,true,error,&sparse))return false;
        if(!gpu.Run([&](std::string&){gpu.Use(evidenceProgram);gpu.Bind(0,dense.buffer);gpu.Bind(1,pose.buffer);gpu.Bind(2,evidence);
            gpu.I("count",count);gpu.I("view",accepted);gpu.F2("size",float(reference.width),float(reference.height));
            gpu.Dispatch((count+63)/64);return true;},error))return false;
        ++accepted;
        acceptedSources.push_back(index);
    }
    if(!accepted)return unavailable("image tracks do not support stable camera geometry.");
    GLuint result=0,statistics=0,fuse=0,gates=0;
    if(!gpu.Run([&](std::string& e){result=gpu.Buffer(std::size_t(count)*2*sizeof(float));statistics=gpu.Buffer(40*sizeof(float));
        gates=gpu.Buffer(std::size_t(count)*accepted*sizeof(float));fuse=gpu.Program(Shaders::Fuse,e);return fuse!=0;},error))return false;
    for(unsigned mode:{5u,0u,1u,2u,4u,3u})if(!gpu.Run([&](std::string&){
        gpu.Use(fuse);gpu.Bind(0,evidence);gpu.Bind(1,result);gpu.Bind(2,statistics);gpu.Bind(3,reference.buffer);
        gpu.Bind(4,gates);
        gpu.I("count",count);gpu.I("views",accepted);gpu.I("mode",mode);gpu.I2("size",map->width,map->height);
        gpu.I2("guideSize",reference.width,reference.height);gpu.Dispatch(mode==0||mode==2||mode==5?1:(count+63)/64);return true;
    },error))return false;
    GLuint completed=0,completion=0;
    if(!gpu.Run([&](std::string& e){completed=gpu.Buffer(std::size_t(count)*2*sizeof(float));completion=gpu.Program(Shaders::Complete,e);return completion!=0;},error))return false;
    for(int mode=0;mode<2;++mode)if(!gpu.Run([&](std::string&){
        gpu.Use(completion);gpu.Bind(0,result);gpu.Bind(1,completed);gpu.Bind(2,reference.buffer);gpu.Bind(3,statistics);
        gpu.I2("size",map->width,map->height);gpu.I2("guideSize",reference.width,reference.height);gpu.I("mode",mode);
        gpu.Dispatch(mode==0?(map->width+15)/16:1,mode==0?(map->height+15)/16:1);return true;
    },error))return false;
    result=completed;
    std::array<float,40> stats{};std::vector<std::array<float,2>> pixels(count);std::vector<float> rejection(std::size_t(count)*accepted);
    if(!gpu.Run([&](std::string&){gpu.Read(statistics,sizeof(stats),stats.data());gpu.Read(result,pixels.size()*sizeof(pixels[0]),pixels.data());
        gpu.Read(gates,rejection.size()*sizeof(float),rejection.data());return true;},error))return false;
    if(stats[35]<=0)return unavailable("too little consistent depth separation or measured coverage.");
    map->inverseDepth.resize(count);map->confidence.resize(count);
    for(unsigned i=0;i<count;++i){map->inverseDepth[i]=pixels[i][0];map->confidence[i]=pixels[i][1];}
    for(unsigned v=0;v<accepted;++v)map->acceptedPairs+=stats[v*4+1]>0;
    map->measuredFraction=stats[34];
    for(unsigned v=0;v<accepted;++v){auto gate=std::make_shared<DepthCorrespondenceSupport>();gate->width=map->width;gate->height=map->height;
        gate->rejected.resize(count);for(unsigned p=0;p<count;++p)gate->rejected[p]=static_cast<std::uint8_t>(rejection[std::size_t(v)*count+p]);
        data.sources[acceptedSources[v]].depthSupport=std::move(gate);}
    const std::array<unsigned,5> dimensions={map->width,map->height,map->sensorWidth,map->sensorHeight,map->acceptedPairs};
    data.depthIdentity=Stack::NodeMath::Sha256ContentIdentity({
        std::string_view(reinterpret_cast<const char*>(dimensions.data()),sizeof(dimensions)),
        std::string_view(reinterpret_cast<const char*>(pixels.data()),pixels.size()*sizeof(pixels[0])),
        std::string_view(reinterpret_cast<const char*>(rejection.data()),rejection.size()*sizeof(float))});
    map->message="GPU relative depth from "+std::to_string(map->acceptedPairs)+" views; "+std::to_string(int(map->measuredFraction*100))+
        "% directly measured, "+std::to_string(int(stats[38]*100))+"% including image-guided estimates. Uncertain regions stay sharp.";
    diagnostics.push_back(map->message);return true;
}
}
