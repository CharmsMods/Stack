#pragma once
#include "Raw/Bracketing/DetailFusion.h"
#include <random>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingDetailFusion() {
    using namespace Raw::Bracketing;
    constexpr unsigned width=128,height=96,frames=5;
    constexpr double variance=.002*.002;
    const auto truth=[](unsigned x,unsigned y) {return .1+(x>40+.31*y?.3:0.);};
    std::mt19937 random(92435);std::normal_distribution<double> noise(0,std::sqrt(variance));
    std::vector<std::vector<Observation>> sources(frames,std::vector<Observation>(width*height));
    for(unsigned f=0;f<frames;++f) for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        auto& s=sources[f][y*width+x];const double distance=double(x)-40-.31*y;
        s.value=(f? .1+.3*.5*(1+std::erf(distance/(std::sqrt(2.)*2.))):truth(x,y))+noise(random);
        s.measurementVariance=s.variance=variance;s.support=1;s.finite=true;s.headroom=1;
    }
    double oldError=0,newError=0,measured=0,reported=0,bias=0;unsigned count=0;
    for(unsigned y=4;y<height-4;++y) for(unsigned x=4;x<width-4;++x) {
        DetailFusionAccumulator selective,equal;double average=0;
        for(unsigned f=0;f<frames;++f) {
            const auto& s=sources[f][y*width+x];const auto coarse=CoarseSample(sources[f],width,height,x,y,1.);
            selective.Add(s,coarse,f?.1:1.);equal.Add(s,coarse,1.);average+=s.value/frames;
        }
        Observation result,unchanged;selective.Finish(result);equal.Finish(unchanged);
        if(std::abs(unchanged.value-average)>1e-12||std::abs(unchanged.measurementVariance-variance/frames)>1e-12)
            throw std::runtime_error("Equal detail preferences changed the temporal mean or variance");
        const double distance=std::abs(double(x)-40-.31*y);
        if(distance<4) {oldError+=std::pow(average-truth(x,y),2);newError+=std::pow(result.value-truth(x,y),2);}
        if(distance>12) {measured+=std::pow(result.value-truth(x,y),2);reported+=result.measurementVariance;
            bias+=result.value-truth(x,y);++count;}
    }
    if(newError>=oldError*.7||std::abs(bias/count)>.0001||measured/reported<.7||measured/reported>1.3)
        throw std::runtime_error("Detail-aware temporal fusion failed sharpness, mean or variance validation");
    std::cout<<"Detail fusion: edge MSE ratio "<<newError/oldError<<", measured/reported variance "<<measured/reported<<'\n';
}
}
