#include "Kernel.h"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <thread>
#include <system_error>
#include <limits>

namespace Raw::Bracketing::Sr {
namespace {
bool ValidSupport(const FrameRegion& f) {
    const auto& s=f.support;
    if(s.mode==CaptureSupportMask::Mode::NotRequired||s.mode==CaptureSupportMask::Mode::AllRejected)
        return f.supportRequired==(s.mode==CaptureSupportMask::Mode::AllRejected)&&
            !s.width&&!s.height&&!s.cellStepRawPixels&&!s.originRawX&&!s.originRawY&&
            !s.rawWidth&&!s.rawHeight&&s.cells.empty();
    if(s.mode!=CaptureSupportMask::Mode::Raster||!f.supportRequired||!s.width||!s.height||
       s.width>std::numeric_limits<std::size_t>::max()/s.height||
       s.cells.size()!=std::size_t(s.width)*s.height||
       s.cellStepRawPixels<2||s.cellStepRawPixels>32||s.cellStepRawPixels%2||
       s.rawWidth<2||s.rawHeight<2||s.rawWidth%2||s.rawHeight%2||
       s.originRawX>=s.rawWidth||s.originRawY>=s.rawHeight||
       s.originRawX%s.cellStepRawPixels||s.originRawY%s.cellStepRawPixels)return false;
    const auto remainingX=s.rawWidth-s.originRawX,remainingY=s.rawHeight-s.originRawY;
    if(s.width>1+(remainingX-1)/s.cellStepRawPixels||s.height>1+(remainingY-1)/s.cellStepRawPixels)return false;
    // CPU and GLSL must describe identical categorical boundaries. Source
    // geometry outside the shader's exact integer range is not approximated.
    for(const auto value:{s.originRawX,s.originRawY,s.rawWidth,s.rawHeight})
        if(value>(std::uint64_t(1)<<53)||double(float(value))!=double(value))return false;
    for(const auto value:s.cells)if(value!=0&&value!=1)return false;
    return true;
}
bool Supports(const CaptureSupportMask& s,float x,float y) {
    if(s.mode==CaptureSupportMask::Mode::NotRequired)return true;
    if(s.mode!=CaptureSupportMask::Mode::Raster||!s.cellStepRawPixels||!std::isfinite(x)||!std::isfinite(y)||
       x<0||y<0||double(x)>=s.rawWidth||double(y)>=s.rawHeight||
       double(x)<s.originRawX||double(y)<s.originRawY)return false;
    const double u=(double(x)-s.originRawX)/s.cellStepRawPixels;
    const double v=(double(y)-s.originRawY)/s.cellStepRawPixels;
    if(u>=s.width||v>=s.height)return false;
    const auto index=std::size_t(v)*s.width+std::size_t(u);
    return index<s.cells.size()&&s.cells[index]==1;
}
bool ValidDepthSupport(const Tile& t,const FrameRegion& f) {
    const auto& depth=f.depthSupport;
    if(!f.depthSupportRequired)return !depth.width&&!depth.height&&!depth.originX&&!depth.originY&&!depth.step&&depth.values.empty();
    if(depth.width!=t.width||depth.height!=t.height||depth.originX!=t.originX||
       depth.originY!=t.originY||depth.step!=t.step||depth.values.size()!=t.pixels.size())return false;
    for(const float value:depth.values)if(value!=0&&value!=1)return false;
    return true;
}
}
bool Validate(const Tile& t,const FrameRegion& f,std::string& error) {
    if(!t.width||!t.height||t.pixels.size()!=std::size_t(t.width)*t.height||
       !(t.step>0)||!std::isfinite(t.step)||!std::isfinite(t.originX)||!std::isfinite(t.originY)||
       f.width<=0||f.height<=0||f.samples.size()!=std::size_t(f.width)*f.height||
       f.mapWidth<2||f.mapHeight<2||f.mapping.size()!=std::size_t(f.mapWidth)*f.mapHeight||
       !(f.mapStep>0)||!std::isfinite(f.mapStep)||!std::isfinite(f.mapOriginX)||!std::isfinite(f.mapOriginY)||
       !std::isfinite(f.detailPreference)||f.detailPreference<.1f||f.detailPreference>1||
       !ValidSupport(f)||!ValidDepthSupport(t,f)) {
        error="Invalid super-resolution tile or capture region.";return false;
    }
    return true;
}
namespace {
Vec4 Map(const FrameRegion& f,float x,float y) {
    const float u=std::clamp((x-f.mapOriginX)/f.mapStep,0.f,float(f.mapWidth-1));
    const float v=std::clamp((y-f.mapOriginY)/f.mapStep,0.f,float(f.mapHeight-1));
    const unsigned ix=std::min(unsigned(u),f.mapWidth-2),iy=std::min(unsigned(v),f.mapHeight-2);
    const float a=u-ix,b=v-iy;
    const auto lerp=[](Vec4 p,Vec4 q,float w){return Vec4{p.x+(q.x-p.x)*w,p.y+(q.y-p.y)*w,p.z+(q.z-p.z)*w,p.w+(q.w-p.w)*w};};
    const auto p00=f.mapping[iy*f.mapWidth+ix],p10=f.mapping[iy*f.mapWidth+ix+1];
    const auto p01=f.mapping[(iy+1)*f.mapWidth+ix],p11=f.mapping[(iy+1)*f.mapWidth+ix+1];
    auto result=lerp(lerp(p00,p10,a),lerp(p01,p11,a),b);
    // Interpolation cannot turn a contributing hard rejection into support.
    if((a<1&&b<1&&p00.z<=0)||(a>0&&b<1&&p10.z<=0)||
       (a<1&&b>0&&p01.z<=0)||(a>0&&b>0&&p11.z<=0))result.z=0;
    return result;
}
}
void AccumulatePixel(const Tile& tile,const FrameRegion& f,unsigned x,unsigned y,PixelAccumulator& p) {
    const auto output=std::size_t(y)*tile.width+x;
    if(f.depthSupportRequired&&(output>=f.depthSupport.values.size()||f.depthSupport.values[output]!=1))return;
    if(f.supportRequired&&f.support.mode==CaptureSupportMask::Mode::NotRequired)return;
    if(!Supports(f.support,tile.originX+x*tile.step,tile.originY+y*tile.step))return;
    const auto m=Map(f,tile.originX+x*tile.step,tile.originY+y*tile.step);
    if(!std::isfinite(m.x)||!std::isfinite(m.y)||!std::isfinite(m.z)||!std::isfinite(m.w)||m.z<=0)return;
    const int cx=int(std::floor(m.x+.5f)),cy=int(std::floor(m.y+.5f));
    // Reject a capture's incomplete color footprint as a whole. Dropping only
    // clipped green taps while retaining red/blue sky taps reconstructs a
    // magenta edge. The same shoulder weight must apply to every color too.
    float headroom=1;std::array<bool,3> usable{};
    for(int yy=cy-3;yy<=cy+3;++yy)for(int xx=cx-3;xx<=cx+3;++xx) {
        if(xx<f.left||yy<f.top||xx>=f.left+f.width||yy>=f.top+f.height)continue;
        const auto s=f.samples[std::size_t(yy-f.top)*f.width+xx-f.left];
        if(s.w>=0)headroom=std::min(headroom,s.w);
        if(s.w>0)usable[f.colors[((yy&1)<<1)|(xx&1)]]=true;
    }
    if(headroom<=0||!usable[0]||!usable[1]||!usable[2])return;
    std::array<float,3> weights{},broadWeights{};
    for(int yy=cy-3;yy<=cy+3;++yy) for(int xx=cx-3;xx<=cx+3;++xx) {
        if(xx<f.left||yy<f.top||xx>=f.left+f.width||yy>=f.top+f.height)continue;
        const auto s=f.samples[std::size_t(yy-f.top)*f.width+xx-f.left];
        if(s.w<=0)continue;
        const unsigned c=f.colors[((yy&1)<<1)|(xx&1)];
        const float dx=xx-m.x,dy=yy-m.y;
        const float variance=std::max(1e-10f,s.y*std::max(0.f,p.kernel.w)+s.z);
        // Spatial mass stays independent of noisy sample values. The fixed
        // variance scale keeps sums in a comfortable float32 range.
        const float base=m.z*headroom*1e-5f/(variance+std::max(0.f,m.w));
        const float w=f.detailPreference*base*std::exp(-.5f*(p.kernel.x*dx*dx+2*p.kernel.y*dx*dy+p.kernel.z*dy*dy));
        const float bw=base*std::exp(-.5f*(dx*dx+dy*dy));
        auto& n=p.narrow[c];auto& b=p.broad[c];
        n.value+=w*s.x;n.weight+=w;n.noise+=w*w*variance;weights[c]+=w;
        b.value+=bw*s.x;b.weight+=bw;b.noise+=bw*bw*variance;broadWeights[c]+=bw;
        p.detailBroadWeight[c]+=bw*f.detailPreference;
        p.crossNoise[c]+=w*bw*variance;
    }
    for(unsigned c=0;c<3;++c) {
        p.narrow[c].frameWeightSquared+=weights[c]*weights[c];
        p.broad[c].frameWeightSquared+=broadWeights[c]*broadWeights[c];
        // Registration and radiometric errors are shared by this capture's
        // neighboring taps, unlike independent sensor measurement noise.
        p.narrowUncertainty[c]+=weights[c]*weights[c]*std::max(0.f,m.w);
        p.broadUncertainty[c]+=broadWeights[c]*broadWeights[c]*std::max(0.f,m.w);
        p.crossFrameWeight[c]+=weights[c]*broadWeights[c];
        p.crossUncertainty[c]+=weights[c]*broadWeights[c]*std::max(0.f,m.w);
    }
}
bool AccumulateCpu(Tile& t,const FrameRegion& f,const std::function<bool()>& cancel,std::string& error,unsigned workers) {
    if(!Validate(t,f,error))return false;
    workers=std::max(1u,std::min({workers,std::max(1u,std::thread::hardware_concurrency()),std::max(1u,t.height/16)}));
    std::atomic<unsigned> row{0};std::atomic<bool> canceled{false};
    auto run=[&] {
        for(unsigned y=row.fetch_add(1);y<t.height;y=row.fetch_add(1)) {
            if(cancel&&cancel()){canceled=true;return;}
            for(unsigned x=0;x<t.width;++x)AccumulatePixel(t,f,x,y,t.pixels[std::size_t(y)*t.width+x]);
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(workers-1);
    try {for(unsigned w=1;w<workers;++w)threads.emplace_back(run);}catch(const std::system_error&) {}
    run();for(auto& thread:threads)thread.join();
    if(canceled){error="Canceled";return false;}
    return true;
}
namespace {
float ResolveAlpha(const PixelAccumulator& p,float samplingConfidence) {
    // Use the same continuous coverage decision for all colors. Switching a
    // sparse color independently between kernels creates false-color contours.
    // Uniformly distributed samples have narrow/broad mass equal to the ratio
    // of Gaussian areas. A deficit signals missing sampling phases.
    const float area=1/std::sqrt(std::max(1e-12f,p.kernel.x*p.kernel.z-p.kernel.y*p.kernel.y));
    float confidence=std::isfinite(samplingConfidence)?std::clamp(samplingConfidence,0.f,1.f):0;
    for(unsigned c=0;c<3;++c) {
        const auto& nc=p.narrow[c];
        // Compare areas using the same capture preference on both kernels.
        // Broad reconstruction itself still receives every usable capture.
        const float coverage=nc.weight/std::max(1e-12f,p.detailBroadWeight[c]*area);
        const float support=nc.weight*nc.weight/std::max(1e-30f,nc.frameWeightSquared);
        confidence=std::min({confidence,std::clamp((coverage-.2f)/.8f,0.f,1.f),std::clamp((support-1.f)/2.f,0.f,1.f)});
    }
    return confidence*confidence*(3-2*confidence);
}
ChannelResult ResolveChannel(const PixelAccumulator& p,unsigned channel,float alpha) {
    const auto& n=p.narrow[channel];const auto& b=p.broad[channel];
    if(!(b.weight>1e-12f))return {};
    const float a=n.weight>1e-12f?alpha/n.weight:0,beta=(1-alpha)/b.weight;
    const float variance=a*a*n.noise+beta*beta*b.noise+2*a*beta*p.crossNoise[channel];
    const float frameSquared=a*a*n.frameWeightSquared+beta*beta*b.frameWeightSquared+2*a*beta*p.crossFrameWeight[channel];
    const float uncertainty=a*a*p.narrowUncertainty[channel]+beta*beta*p.broadUncertainty[channel]+2*a*beta*p.crossUncertainty[channel];
    return {a*n.value+beta*b.value,variance,1/std::max(1e-30f,frameSquared),true,uncertainty};
}
}
ChannelResult Resolve(const PixelAccumulator& p,unsigned channel,float samplingConfidence) {
    return ResolveChannel(p,channel,ResolveAlpha(p,samplingConfidence));
}
std::array<ChannelResult,3> ResolveChannels(const PixelAccumulator& p,float samplingConfidence) {
    const float alpha=ResolveAlpha(p,samplingConfidence);
    return {ResolveChannel(p,0,alpha),ResolveChannel(p,1,alpha),ResolveChannel(p,2,alpha)};
}
}
