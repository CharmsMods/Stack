#include "Raw/Bracketing/RadiometricConsistency.h"
#include "Raw/Bracketing/PhotometricReliability.h"
#include "Raw/Bracketing/SuperResolution/SamplingCoverage.h"
#include "Raw/Bracketing/SuperResolution/Kernel.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Raw::Bracketing;
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-6) {return std::abs(a-b)<=tolerance;}

void CalibrationChecks() {
    double ev=0,variance=0;
    const std::array<double,3> noise={1e-5,1e-5,1e-5};
    Check(ConsistentExposureRatio({1,1,1},noise,{true,true,true},ev,variance)&&Near(ev,1),
        "A common one-stop exposure change was rejected");
    Check(variance>0&&variance<noise[0],"Exposure uncertainty was lost");
    Check(!ConsistentExposureRatio({1.15,1,.85},noise,{true,true,true},ev,variance),
        "Changing colour balance was accepted as a scalar exposure");
    Check(ConsistentExposureRatio({-2,-2,0},noise,{true,true,false},ev,variance)&&Near(ev,-2),
        "Two usable colours could not calibrate a dark third channel");
    Check(!ConsistentExposureRatio({1,0,0},noise,{true,false,false},ev,variance),
        "A single colour established exposure calibration");
    Check(ConsistentExposureRatio({1,1,1.2},{1e-5,1e-5,.04},{true,true,true},ev,variance)&&std::abs(ev-1)<.001,
        "A noisy colour overruled the reliable channels");
    Check(!ConsistentExposureRatio({1,std::numeric_limits<double>::quiet_NaN(),1},noise,
        {true,true,true},ev,variance),"Non-finite exposure evidence was accepted");

    Check(Near(GreenDifferenceResidual(.2,.01,.2,.01),0),
        "Common green exposure error became a green split");
    const double split=GreenDifferenceResidual(.2,.01,-.2,.01);
    Check(Near(split,.4/std::sqrt(.02)),"Green split did not retain its noise units");
    Check(std::abs(GreenDifferenceResidual(.2,1,-.2,1))<std::abs(split),
        "Green consistency ignored measurement uncertainty");
    Check(NoiseCorrectedMismatch(49*split,49*split*split,49)>7,
        "A coherent opposing-green change escaped the shared rejection gate");
    std::cout<<"Calibration and paired-green checks passed\n";
}

void CoverageChecks() {
    Sr::SamplingCoverage repeated,horizontal,diagonal,opposite,lattice,shifted,half,dominated;
    for(unsigned i=0;i<16;++i) {
        repeated.Add(0,0,1);horizontal.Add(i*.125,0,1);
        diagonal.Add(i*.125,i*.125,1);opposite.Add(i*.125,-i*.125,1);
        lattice.Add(i%2,(i/2)%2,1);
        shifted.Add(i%2+6.37,(i/2)%2-8.19,1);
        half.Add((i%4)*.5,(i/4)*.5,1);
        dominated.Add(i%2,(i/2)%2,i==0?1:1e-6);
    }
    Check(repeated.Confidence()<1e-6,"Repeated captures were treated as new Bayer phases");
    Check(horizontal.Confidence()<1e-6&&diagonal.Confidence()<1e-6&&opposite.Confidence()<1e-6,
        "Collinear samples claimed two-dimensional coverage");
    Check(lattice.Confidence()>.999&&half.Confidence()>.999,"Distributed Bayer phases lost coverage");
    Check(Near(lattice.Confidence(),shifted.Confidence()),"Phase origin changed coverage");
    Check(dominated.Confidence()<.001,"An unreliable burst claimed independent coverage");
    Sr::SamplingCoverage single;single.Add(.5,.5,1);single.Add(1,1,0);
    single.Add(0,0,std::numeric_limits<double>::quiet_NaN());
    Check(single.Confidence()==0,"Missing evidence increased sampling confidence");
    Sr::SamplingCoverage native(1),enlarged(2),subpixel(2);
    for(unsigned i=0;i<16;++i) {
        native.Add(i%2,(i/2)%2,1);enlarged.Add(i%2,(i/2)%2,1);
        subpixel.Add((i%4)*.5,(i/4)*.5,1);
    }
    Check(native.Confidence()>.999&&enlarged.Confidence()<1e-6,
        "Whole-pixel shifts claimed half-pixel sampling for a 2x output");
    Check(subpixel.Confidence()>.999,"Sixteen half-pixel phases lost 2x sampling coverage");
    std::cout<<"Local Bayer phase coverage checks passed\n";
}

Sr::Tile Tile() {
    Sr::Tile t;t.width=t.height=4;t.originX=t.originY=6;t.step=.5;
    t.pixels.resize(t.width*t.height);
    for(auto& p:t.pixels)p.kernel={4,0,4,.3f};
    return t;
}
Sr::FrameRegion Frame(float dx=0,float dy=0) {
    Sr::FrameRegion f;f.width=f.height=20;f.mapWidth=f.mapHeight=4;f.colors={0,1,1,2};
    f.mapping.resize(16);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)f.mapping[y*4+x]={x*8+dx,y*8+dy,1,1e-6f};
    f.samples.resize(400);
    for(int y=0;y<20;++y)for(int x=0;x<20;++x) {
        const auto c=f.colors[((y&1)<<1)|(x&1)];
        f.samples[y*20+x]={.2f+c*.05f,0,1e-4f,1};
    }
    return f;
}
void KernelChecks() {
    std::string error;Sr::GpuKernel gpu;double maximumError=0;
    // All cases use actual accumulation on CPU and GPU, including the changed
    // clipping/defect prepass and a shared geometry decision at resolve.
    for(unsigned scenario=0;scenario<4;++scenario) {
        auto cpu=Tile(),accelerated=cpu;
        Check(gpu.Begin(accelerated,error),error.c_str());
        for(unsigned i=0;i<4;++i) {
            auto frame=Frame(i%2,(i/2)%2);
            if(scenario==1)frame.samples[7*20+7]={std::numeric_limits<float>::quiet_NaN(),0,0,-1};
            if(scenario==2)frame.samples[6*20+7].w=0; // one clipped green
            if(scenario==3)for(int y=0;y<20;++y)for(int x=0;x<20;++x)
                if(frame.colors[((y&1)<<1)|(x&1)]==0)frame.samples[y*20+x].w=-1;
            Check(Sr::AccumulateCpu(cpu,frame,{},error),error.c_str());
            Check(gpu.Add(accelerated,frame,error),error.c_str());
        }
        Check(gpu.Read(accelerated,error),error.c_str());gpu.Release();
        for(std::size_t p=0;p<cpu.pixels.size();++p)for(unsigned c=0;c<3;++c) {
            for(float geometry:{0.f,.4f,1.f}) {
                const auto a=Sr::Resolve(cpu.pixels[p],c,geometry),b=Sr::Resolve(accelerated.pixels[p],c,geometry);
                Check(a.valid==b.valid,"CPU/GPU capture acceptance differs");
                if(scenario>=2) {Check(!a.valid,"Incomplete/clipped colour footprint was accepted");continue;}
                Check(a.valid&&Near(a.value,.2+c*.05,2e-6),"Missing defect biased or discarded a constant scene");
                Check(a.variance>0&&a.uncertainty>0&&a.support>1,"Noise or capture support was lost");
                maximumError=std::max(maximumError,double(std::abs(a.value-b.value)));
                Check(Near(a.value,b.value,2e-6)&&Near(a.variance,b.variance,1e-8)&&
                    Near(a.support,b.support,2e-5)&&Near(a.uncertainty,b.uncertainty,1e-8),
                    "CPU/GPU reconstruction or uncertainty differs");
                if(geometry==0) {
                    const auto& broad=cpu.pixels[p].broad[c];
                    Check(Near(a.value,broad.value/broad.weight)&&
                        Near(a.variance,broad.noise/(broad.weight*broad.weight),1e-8),
                        "Poor phase coverage did not select the broad reconstruction");
                }
            }
        }
    }
    std::cout<<"CPU/GPU defect, clipping, resolve and uncertainty checks passed; maximum value difference "
        <<maximumError<<'\n';
}
}

int main() {
    GLFWwindow* window=nullptr;int result=1;
    try {
        CalibrationChecks();CoverageChecks();
        Check(glfwInit()!=0,"Could not initialize the focused GPU checks");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(32,32,"Burst quality checks",nullptr,nullptr);
        Check(window!=nullptr,"Could not create the focused OpenGL context");
        glfwMakeContextCurrent(window);Check(LoadGLFunctions(),"Could not load OpenGL functions");
        KernelChecks();result=0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';}
    if(window)glfwDestroyWindow(window);glfwTerminate();return result;
}
