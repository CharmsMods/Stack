#pragma once

#include "Raw/Bracketing/SuperResolution/Kernel.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
namespace SrClipping {
namespace Sr=Raw::Bracketing::Sr;
inline void Require(bool ok,const std::string& message) {
    if(!ok)throw std::runtime_error("SR clipping reconstruction: "+message);
}
inline Sr::Tile Point(float x=6.25f,float y=6.25f) {
    Sr::Tile tile;tile.width=tile.height=1;tile.originX=x;tile.originY=y;tile.step=1;
    tile.pixels.resize(1);tile.pixels[0].kernel={4,0,4,.7f};
    tile.pixels[0].broadKernel={1,0,1,0};return tile;
}
inline double Signal(double x,unsigned color,bool colored) {
    return colored?.7-.04*color+(.1+.015*color)*(x-6):.7+.1*(x-6);
}
inline Sr::FrameRegion Ramp(double exposure=1,bool colored=true) {
    Sr::FrameRegion frame;frame.width=frame.height=20;frame.mapWidth=frame.mapHeight=4;
    frame.colors={0,1,1,2};frame.mapping.resize(16);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
        frame.mapping[y*4+x]={float(x*8),float(y*8),1,1e-6f};
    frame.samples.resize(400);
    for(int y=0;y<20;++y)for(int x=0;x<20;++x) {
        const auto color=frame.colors[((y&1)<<1)|(x&1)];
        const double signal=Signal(x,color,colored),observed=std::min(1.,exposure*signal);
        double h=std::clamp((.995-observed)/.045,0.,1.);h=h*h*(3-2*h);
        // A clipped sensor value is a lower bound in calibrated radiance
        // units. It is retained for model checks, never as a fit observation.
        frame.samples[y*20+x]={float(observed/exposure),0,float(1e-4/(exposure*exposure)),float(h)};
    }
    return frame;
}
}

inline void ValidateSuperResolutionClipping() {
    using namespace SrClipping;
    std::string error;Sr::GpuKernel gpu;
    struct Cleanup {Sr::GpuKernel& gpu;~Cleanup(){gpu.Release();}} cleanup{gpu};
    double maximumValueError=0,maximumVarianceError=0,maximumSceneError=0;
    const auto compare=[&](const Sr::Tile& cpu,const Sr::Tile& accelerated,bool supported) {
        for(float geometry:{0.f,.4f,1.f})for(unsigned c=0;c<3;++c) {
            const auto a=Sr::Resolve(cpu.pixels[0],c,geometry);
            const auto b=Sr::Resolve(accelerated.pixels[0],c,geometry);
            Require(a.valid==supported&&b.valid==supported,"CPU/GPU support eligibility differs");
            if(!supported)continue;
            maximumValueError=std::max(maximumValueError,double(std::abs(a.value-b.value)));
            maximumVarianceError=std::max(maximumVarianceError,double(std::abs(a.variance-b.variance)));
            Require(std::abs(a.value-b.value)<3e-6&&std::abs(a.variance-b.variance)<2e-8&&
                std::abs(a.uncertainty-b.uncertainty)<2e-8&&std::abs(a.support-b.support)<3e-5,
                "CPU/GPU value, noise, model risk or independent-capture support differs");
            Require(a.variance>0&&a.uncertainty>0&&std::isfinite(a.value),
                "recovered samples lost uncertainty or finite scene values");
        }
    };
    const auto accumulate=[&](const Sr::FrameRegion& frame,Sr::Tile& cpu,Sr::Tile& accelerated) {
        Require(Sr::AccumulateCpu(cpu,frame,{},error),error);
        Require(gpu.Begin(accelerated,error)&&gpu.Add(accelerated,frame,error)&&
            gpu.Read(accelerated,error),error);
    };

    auto recovered=Point(),recoveredGpu=recovered;
    const auto partial=Ramp();
    accumulate(partial,recovered,recoveredGpu);compare(recovered,recoveredGpu,true);
    for(unsigned c=0;c<3;++c) {
        const auto result=Sr::Resolve(recovered.pixels[0],c);
        maximumSceneError=std::max(maximumSceneError,std::abs(result.value-Signal(recovered.originX,c,true)));
        Require(std::abs(result.value-Signal(recovered.originX,c,true))<3e-6,
            "asymmetric clipping biased an affine color signal");
        Require(std::abs(result.support-1)<2e-5&&recovered.pixels[0].narrow[c].weight==0,
            "a partial footprint claimed fine-resolution or extra capture support");
    }
    // Two independent captures with identical test values count as two
    // captures, not all neighboring sensor taps. Their predicted variance halves.
    auto repeated=recovered,repeatedGpu=recovered;
    accumulate(partial,repeated,repeatedGpu);compare(repeated,repeatedGpu,true);
    for(unsigned c=0;c<3;++c) {
        const auto once=Sr::Resolve(recovered.pixels[0],c),twice=Sr::Resolve(repeated.pixels[0],c);
        Require(std::abs(twice.support-2)<2e-5&&std::abs(twice.variance-.5f*once.variance)<1e-8,
            "partial-footprint variance or capture support did not combine correctly");
    }

    for(unsigned scenario=0;scenario<8;++scenario) {
        auto frame=partial;auto cpu=Point(),accelerated=cpu;
        if(scenario==0)frame.samples[6*20+6]={1,0,1e-4f,0};
        if(scenario==1)for(auto& sample:frame.samples)sample={1,0,1e-4f,0};
        if(scenario==2) {
            // Flat surviving data cannot explain a distant bright clipped
            // surface. The fit must respect its radiometric lower bound.
            for(auto& sample:frame.samples)if(sample.w>0)sample.x=.1f;
        }
        if(scenario==3)for(int y=0;y<20;++y)for(int x=0;x<20;++x)
            if(frame.colors[((y&1)<<1)|(x&1)]==0)frame.samples[y*20+x].w=-1;
        if(scenario==4) {
            frame.supportRequired=true;
            frame.support.mode=Raw::Bracketing::CaptureSupportMask::Mode::AllRejected;
        }
        if(scenario==5)frame.samples[6*20+6]={std::numeric_limits<float>::quiet_NaN(),0,0,-1};
        if(scenario==6)frame.samples[5*20+9]={0,0,0,-2};
        if(scenario==7)for(int y=0;y<20;++y)for(int x=0;x<20;++x) {
            auto& sample=frame.samples[y*20+x];
            if(sample.w>0)sample.x+=((x/2+y/2)&1)?.1f:-.1f;
        }
        accumulate(frame,cpu,accelerated);compare(cpu,accelerated,false);
    }
    // Preserve the existing sharp step/partially clipped-green safeguard. A
    // common plane cannot span these two surfaces without a large residual.
    auto step=Ramp(.5);auto stepCpu=Point(),stepGpu=stepCpu;
    for(int y=0;y<20;++y)for(int x=0;x<20;++x) {
        auto& sample=step.samples[y*20+x];sample.x=x<8?.05f:.9f;
        if(x>=8&&step.colors[((y&1)<<1)|(x&1)]==1){sample.x=1;sample.w=0;}
    }
    accumulate(step,stepCpu,stepGpu);compare(stepCpu,stepGpu,false);

    // Sweep the actual sensor shoulder with a consistent calibrated ramp.
    // A complete but noisier capture has a small offset, making a source
    // re-entry at exactly zero headroom visible in both value and variance.
    std::array<std::array<float,3>,3> previous{};
    std::array<std::array<float,3>,3> previousVariance{};
    bool first=true;double maximumShoulderStep=0,maximumVarianceStep=0;
    for(double exposure:{.98,.994,.995,1.0}) {
        auto cpu=Point(6,6),accelerated=cpu;
        auto shortExposure=Ramp(.25,false);
        for(auto& sample:shortExposure.samples)sample.x+=.02f;
        const auto longExposure=Ramp(exposure,false);
        Require(Sr::AccumulateCpu(cpu,shortExposure,{},error)&&
            Sr::AccumulateCpu(cpu,longExposure,{},error),error);
        Require(gpu.Begin(accelerated,error)&&gpu.Add(accelerated,shortExposure,error)&&
            gpu.Add(accelerated,longExposure,error)&&gpu.Read(accelerated,error),error);
        compare(cpu,accelerated,true);
        unsigned g=0;
        for(float geometry:{0.f,.4f,1.f}) {
            for(unsigned c=0;c<3;++c) {
                const auto result=Sr::Resolve(cpu.pixels[0],c,geometry);
                if(!first) {
                    maximumShoulderStep=std::max(maximumShoulderStep,double(std::abs(result.value-previous[g][c])));
                    maximumVarianceStep=std::max(maximumVarianceStep,double(std::abs(result.variance-previousVariance[g][c])));
                    Require(std::abs(result.value-previous[g][c])<.004,
                        "a capture abruptly re-entered at the clipping shoulder");
                }
                Require(result.value<.71f,"retained long-exposure evidence was lost at the shoulder");
                previous[g][c]=result.value;previousVariance[g][c]=result.variance;
            }
            ++g;
        }
        first=false;
    }
    std::cout<<"SR clipping: maximum scene error="<<maximumSceneError
        <<", CPU/GPU value error="<<maximumValueError<<", variance error="<<maximumVarianceError
        <<", maximum shoulder step="<<maximumShoulderStep
        <<", variance step="<<maximumVarianceStep<<'\n';
}

} // namespace Stack::Validation
