#include "Raw/Bracketing/SuperResolution/Kernel.h"
#include "Raw/Bracketing/SuperResolution/Reconstruction.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "BracketingDiagnosticInputs.h"
#include "SuperResolutionContracts.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/RawProcessingMath.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace Stack::Validation {
namespace {
using namespace Raw::Bracketing;
void Require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
Sr::Tile TestTile() {
    Sr::Tile t;t.width=96;t.height=80;t.originX=t.originY=8;t.step=.5;
    t.pixels.resize(t.width*t.height);
    for(auto& p:t.pixels)p.kernel={4,0,4,.3f};
    return t;
}
float Scene(float x,float y,unsigned c) {
    return .32f+.09f*std::sin(x*1.8f)+.06f*std::cos(y*1.15f)+c*.025f;
}
Sr::FrameRegion TestFrame(float dx,float dy,int seed,bool flat=false) {
    Sr::FrameRegion f;f.width=80;f.height=72;f.colors={0,1,1,2};
    f.mapWidth=11;f.mapHeight=10;f.mapping.resize(f.mapWidth*f.mapHeight);
    for(unsigned y=0;y<f.mapHeight;++y)for(unsigned x=0;x<f.mapWidth;++x)f.mapping[y*f.mapWidth+x]={x*8+dx,y*8+dy,1,0};
    f.samples.resize(f.width*f.height);std::mt19937 rng(seed);std::normal_distribution<float> noise(0,.008f);
    for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x) {
        const auto c=f.colors[((y&1)<<1)|(x&1)];
        f.samples[y*f.width+x]={ (flat?.3f:Scene(x-dx,y-dy,c))+noise(rng),0,.000064f,1};
    }
    return f;
}
double Error(const Sr::Tile& t,bool flat=false) {
    double e=0;for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x)for(unsigned c=0;c<3;++c) {
        const auto r=Sr::Resolve(t.pixels[y*t.width+x],c);Require(r.valid,"Missing reconstructed color");
        const double d=r.value-(flat?.3f:Scene(t.originX+x*t.step,t.originY+y*t.step,c));e+=d*d;
    }
    return e/(t.width*t.height*3);
}
double TemporalEnlargementError(const Sr::Tile& output) {
    constexpr unsigned w=80,h=72;
    std::vector<float> mosaic(w*h);
    for(unsigned i=0;i<16;++i) {
        const float dx=(i%4)*.5f,dy=(i/4)*.5f;
        const auto frame=TestFrame(dx,dy,i);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const int px=x&1,py=y&1;
            const float u=(x+dx-px)*.5f,v=(y+dy-py)*.5f;
            const int ix=int(std::floor(u)),iy=int(std::floor(v));
            const auto at=[&](int xx,int yy){return frame.samples[(std::clamp(yy,0,int(h/2)-1)*2+py)*w+std::clamp(xx,0,int(w/2)-1)*2+px].x;};
            const auto keys=[](float v){v=std::abs(v);return v<1?1.5f*v*v*v-2.5f*v*v+1:v<2?-.5f*v*v*v+2.5f*v*v-4*v+2:0.f;};
            float value=0;for(int yy=-1;yy<=2;++yy)for(int xx=-1;xx<=2;++xx)value+=keys(u-ix-xx)*keys(v-iy-yy)*at(ix+xx,iy+yy);
            mosaic[y*w+x]+=value/16;
        }
    }
    double error=0;
    for(unsigned y=0;y<output.height;++y)for(unsigned x=0;x<output.width;++x) {
        const float sx=output.originX+x*output.step,sy=output.originY+y*output.step;
        const unsigned ix=unsigned(sx),iy=unsigned(sy);const float a=sx-ix,b=sy-iy;
        const auto p00=Raw::Processing::DemosaicMalvarHeCutlerAt(mosaic,w,h,Raw::CfaPattern::RGGB,ix,iy);
        const auto p10=Raw::Processing::DemosaicMalvarHeCutlerAt(mosaic,w,h,Raw::CfaPattern::RGGB,ix+1,iy);
        const auto p01=Raw::Processing::DemosaicMalvarHeCutlerAt(mosaic,w,h,Raw::CfaPattern::RGGB,ix,iy+1);
        const auto p11=Raw::Processing::DemosaicMalvarHeCutlerAt(mosaic,w,h,Raw::CfaPattern::RGGB,ix+1,iy+1);
        for(unsigned c=0;c<3;++c) {
            const double difference=(1-b)*((1-a)*p00[c]+a*p10[c])+b*((1-a)*p01[c]+a*p11[c])-Scene(sx,sy,c);
            error+=difference*difference;
        }
    }
    return error/(output.width*output.height*3);
}
std::vector<float> Enlarge2x(const std::vector<float>& input,unsigned width,unsigned height) {
    std::vector<float> result(std::size_t(width)*height*12);
    for(unsigned y=0;y<height*2;++y)for(unsigned x=0;x<width*2;++x) {
        const float px=std::clamp((x+.5f)*.5f-.5f,0.f,float(width-1)),py=std::clamp((y+.5f)*.5f-.5f,0.f,float(height-1));
        const unsigned x0=unsigned(px),y0=unsigned(py),x1=std::min(width-1,x0+1),y1=std::min(height-1,y0+1);
        const float fx=px-x0,fy=py-y0;
        for(unsigned c=0;c<3;++c) {
            const auto at=[&](unsigned xx,unsigned yy){return input[(std::size_t(yy)*width+xx)*3+c];};
            result[(std::size_t(y)*width*2+x)*3+c]=(1-fy)*((1-fx)*at(x0,y0)+fx*at(x1,y0))+fy*((1-fx)*at(x0,y1)+fx*at(x1,y1));
        }
    }
    return result;
}
void SaveRgb(const std::filesystem::path& path,const std::vector<float>& rgb,unsigned w,unsigned h,const Raw::RawMetadata& m,double gain=1) {
    std::vector<unsigned char> bytes(std::size_t(w)*h*3);
    for(std::size_t p=0;p<std::size_t(w)*h;++p)for(unsigned c=0;c<3;++c) {
        double v=0;for(unsigned k=0;k<3;++k)v+=m.cameraToSrgb[c*3+k]*rgb[p*3+k]*m.cameraWhiteBalance[k]/std::max(1e-6f,m.cameraWhiteBalance[1]);
        v=std::max(0.,v*gain);v=v/(1+v);v=v<.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;
        bytes[p*3+c]=static_cast<unsigned char>(std::clamp(v,0.,1.)*255+.5);
    }
    Require(stbi_write_png(path.string().c_str(),w,h,3,bytes.data(),w*3)!=0,"Could not write comparison PNG");
}
}
bool ValidateSuperResolution(int argc,char** argv) {
    if(argc<1)return false;
    const std::filesystem::path root=argv[0];std::filesystem::create_directories(root);
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(64,64,"SR validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    bool success=false;
    try {
        std::string error;Sr::GpuKernel gpu;auto cpu=TestTile(),accelerated=cpu,single=cpu,flat=cpu,flatSingle=cpu;
        // Compare stationary noise with the same spatial footprint in both
        // runs. Otherwise a different reconstruction bandwidth changes variance.
        for(auto& p:flat.pixels)p.kernel={1,0,1,.3f};flatSingle=flat;
        Require(gpu.Begin(accelerated,error),error);
        for(unsigned i=0;i<16;++i) {
            auto frame=TestFrame(float(i%4)*.5f,float(i/4)*.5f,i);
            for(auto& m:frame.mapping)m.w=i*1e-6f;
            Require(Sr::AccumulateCpu(cpu,frame,{},error),error);Require(gpu.Add(accelerated,frame,error),error);
            if(i==0)Require(Sr::AccumulateCpu(single,frame,{},error),error);
            const auto noiseFrame=TestFrame(0,0,i,true);Require(Sr::AccumulateCpu(flat,noiseFrame,{},error),error);
            if(i==0)Require(Sr::AccumulateCpu(flatSingle,noiseFrame,{},error),error);
        }
        Require(gpu.Read(accelerated,error),error);gpu.Release();
        double maxGpuError=0;
        for(std::size_t p=0;p<cpu.pixels.size();++p)for(unsigned c=0;c<3;++c)
            maxGpuError=std::max(maxGpuError,double(std::abs(Sr::Resolve(cpu.pixels[p],c).value-Sr::Resolve(accelerated.pixels[p],c).value)));
        const double errorSingle=Error(single),errorBurst=Error(cpu),noiseRatio=Error(flat,true)/Error(flatSingle,true);
        Require(maxGpuError<2e-5,"CPU/GPU reconstruction differs");
        Require(errorBurst<errorSingle*.75,"Shifted samples did not improve fine-detail reconstruction");
        Require(noiseRatio<.10&&noiseRatio>.025,"Stationary burst variance reduction is wrong");
        auto invalid=TestTile();auto missing=TestFrame(0,0,1);
        for(auto& sample:missing.samples)sample.w=-1;
        Require(Sr::AccumulateCpu(invalid,missing,{},error),error);
        for(const auto& p:invalid.pixels)for(unsigned c=0;c<3;++c)Require(!Sr::Resolve(p,c).valid,"All-invalid SR samples became valid");
        // A bright edge clips green before red and blue. Reject its incomplete
        // capture footprint on CPU and GPU, preserving the complete exposure.
        auto complete=TestTile(),clippedCpu=complete,clippedGpu=complete;
        auto clean=TestFrame(0,0,23,true),partial=clean;
        for(int y=0;y<partial.height;++y)for(int x=0;x<partial.width;++x) {
            auto& sample=partial.samples[y*partial.width+x];
            sample.x=x<32?.05f:.9f;
            if(x>=32&&partial.colors[((y&1)<<1)|(x&1)]==1)sample.w=0;
        }
        Require(Sr::AccumulateCpu(complete,clean,{},error),error);
        clippedCpu=complete;clippedGpu=complete;
        Require(Sr::AccumulateCpu(clippedCpu,partial,{},error),error);
        Require(gpu.Begin(clippedGpu,error)&&gpu.Add(clippedGpu,partial,error)&&gpu.Read(clippedGpu,error),error);
        gpu.Release();
        for(unsigned y=0;y<complete.height;++y)for(unsigned x=0;x<complete.width;++x)for(unsigned c=0;c<3;++c) {
            const auto p=y*complete.width+x;
            const auto a=Sr::Resolve(clippedCpu.pixels[p],c),b=Sr::Resolve(clippedGpu.pixels[p],c);
            Require(std::abs(a.value-b.value)<2e-5,"Clipped-color CPU/GPU decisions differ");
            if(complete.originX+x*complete.step>=30)
                Require(std::abs(a.value-Sr::Resolve(complete.pixels[p],c).value)<1e-7,
                    "Partly clipped SR capture contaminated a complete color measurement");
        }
        auto uncertain=Sr::Resolve(cpu.pixels[200],0);
        Require(uncertain.variance>0&&uncertain.uncertainty>0&&uncertain.support>1,"SR lost measurement, registration or support evidence");
        const double temporalError=TemporalEnlargementError(cpu);
        Require(errorBurst<temporalError*.9,"Original-sample SR did not improve over a resampled temporal mosaic");
        nlohmann::json report={{"singleReconstructionMse",errorSingle},{"burstReconstructionMse",errorBurst},{"temporalBicubicMosaicEnlargementMse",temporalError},{"stationaryNoiseRatio16Frames",noiseRatio},{"maximumCpuGpuError",maxGpuError}};
        SrContracts::Validate(root,report);
        std::cout<<report.dump(2)<<'\n'<<std::flush;
        if(argc>1) {
            ProcessingRequest request;Require(LoadBracketingDiagnosticFolder(argv[1],request,error),error);
            const auto memory=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
            request.memoryBudgetBytes=memory.budgetBytes;request.workerCount=ResolveBracketingWorkerCount(memory.budgetBytes);
            request.automaticMemoryBudget=true;
            request.cacheDirectory=root/"prepared";
            request.executeOpenGlTask=[](Raw::OpenGlTask task,std::string& e){return task(e);};
            int last=-1;request.reportProgress=[&](double p,const std::string& stage){const int step=int(p*100);if(step!=last){std::cout<<step<<"% "<<stage<<'\n'<<std::flush;last=step;}};
            auto normal=Process(request);Require(normal.status==BracketingResult::Status::Completed,normal.message);
            request.analysis=normal.analysis;request.recipe.reconstruction=ReconstructionMode::SuperResolution2x;
            auto sr=Process(request);Require(sr.status==BracketingResult::Status::Completed,sr.message);
            Require(sr.raw->metadata.visibleWidth==normal.raw->metadata.visibleWidth*2,"SR dimensions incorrect");
            Require(sr.analysis->prepared==normal.analysis->prepared,"SR discarded reusable analysis");
            report["real"]={{"sourceCount",request.sources.size()},{"width",sr.raw->metadata.visibleWidth},{"height",sr.raw->metadata.visibleHeight},{"timings",sr.analysis->stageSeconds},{"diagnostics",sr.analysis->diagnostics}};
            SaveRgb(root/"sr-overview.png",sr.preview.resultRgb,sr.preview.width,sr.preview.height,sr.preview.metadata);
            SaveRgb(root/"standard-overview.png",normal.preview.resultRgb,normal.preview.width,normal.preview.height,normal.preview.metadata);
            for(unsigned i=0;i<3;++i) {
                const unsigned x=unsigned((.25+.25*i)*sr.raw->metadata.visibleWidth),y=sr.raw->metadata.visibleHeight/2;
                auto detail=SuperResolutionDetail(sr,x,y,512);
                Require(detail.width>0,"Native SR detail is empty");
                SaveRgb(root/("sr-native-"+std::to_string(i)+".png"),detail.resultRgb,detail.width,detail.height,detail.metadata,4);
                const auto& original=*detail.originals.at(request.recipe.originFrameId);
                SaveRgb(root/("origin-native-"+std::to_string(i)+".png"),original.rgb,original.width,original.height,detail.metadata,4);
                auto temporal=RenderNativeDetail(request,x/2,y/2,256);
                auto enlarged=Enlarge2x(temporal.resultRgb,temporal.width,temporal.height);
                Require(enlarged.size()==detail.resultRgb.size(),"Comparison crops have different fields of view");
                std::vector<float> pair(detail.resultRgb.size()*2);
                for(unsigned row=0;row<detail.height;++row) {
                    std::copy_n(enlarged.begin()+row*detail.width*3,detail.width*3,pair.begin()+row*detail.width*6);
                    std::copy_n(detail.resultRgb.begin()+row*detail.width*3,detail.width*3,pair.begin()+row*detail.width*6+detail.width*3);
                }
                SaveRgb(root/("temporal-left-sr-right-"+std::to_string(i)+".png"),pair,detail.width*2,detail.height,detail.metadata,4);
            }
            report["real"]["nativeComparisonExposureEv"]=2;
            if(argc>2&&std::string(argv[2])=="--both-sizes") {
                sr={};
                request.recipe.reconstruction=ReconstructionMode::SuperResolution1x;
                auto one=Process(request);Require(one.status==BracketingResult::Status::Completed,one.message);
                Require(one.raw->metadata.visibleWidth==normal.raw->metadata.visibleWidth&&
                    one.raw->metadata.visibleHeight==normal.raw->metadata.visibleHeight,"SR 1x dimensions incorrect");
                report["real1x"]={{"width",one.raw->metadata.visibleWidth},{"height",one.raw->metadata.visibleHeight},
                    {"timings",one.analysis->stageSeconds},{"diagnostics",one.analysis->diagnostics}};
                SaveRgb(root/"sr-1x-overview.png",one.preview.resultRgb,one.preview.width,one.preview.height,one.preview.metadata);
            }
        }
        std::ofstream(root/"report.json")<<report.dump(2);
        std::cout<<"Super-resolution validation passed.\n";success=true;
    }catch(const std::exception& e){std::cerr<<"Super-resolution validation failed: "<<e.what()<<'\n';}
    glfwDestroyWindow(window);glfwTerminate();return success;
}
}
