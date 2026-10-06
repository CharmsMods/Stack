#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Bracketing::Sr {
// All coordinates and kernels use original sensor pixels. RGB samples are
// calibrated linear camera measurements, before white balance and grading.
struct Vec4 { float x=0,y=0,z=0,w=0; };
struct Moment { float value=0,weight=0,noise=0,frameWeightSquared=0; };
struct PixelAccumulator {
    Vec4 kernel; // inverse covariance xx, xy, yy; stable scene signal pilot
    std::array<Moment,3> narrow{}, broad{};
    std::array<float,4> narrowUncertainty{},broadUncertainty{};
    std::array<float,4> crossNoise{},crossFrameWeight{},crossUncertainty{};
};
struct Tile {
    unsigned width=0,height=0;
    float originX=0,originY=0,step=1;
    std::vector<PixelAccumulator> pixels;
};
struct FrameRegion {
    int left=0,top=0,width=0,height=0;
    unsigned mapWidth=0,mapHeight=0;
    float mapOriginX=0,mapOriginY=0,mapStep=8;
    std::array<int,4> colors{}; // sensor parity to RGB channel
    // Original, unresampled samples: radiance, shot coefficient, read variance,
    // headroom. Negative headroom excludes clipped/invalid samples.
    std::vector<Vec4> samples;
    // Reference to source mapping and reliability: x, y, confidence, risk.
    std::vector<Vec4> mapping;
};
struct ChannelResult { float value=0,variance=0,support=0; bool valid=false; float uncertainty=0; };
bool Validate(const Tile&,const FrameRegion&,std::string&);
void AccumulatePixel(const Tile&,const FrameRegion&,unsigned x,unsigned y,PixelAccumulator&);
bool AccumulateCpu(Tile&,const FrameRegion&,const std::function<bool()>& cancel,std::string&,unsigned workers=1);
ChannelResult Resolve(const PixelAccumulator&,unsigned channel);

// These methods run only on the caller's render-owner context. One tile stays
// resident across captures; only original source regions are streamed to it.
class GpuKernel {
public:
    bool Begin(const Tile&,std::string&);
    bool Add(const Tile&,const FrameRegion&,std::string&);
    bool Read(Tile&,std::string&);
    void Release();
private:
    unsigned program_=0,input_=0,output_=0;
};
}
