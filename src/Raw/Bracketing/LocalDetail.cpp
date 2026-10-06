#include "LocalDetail.h"
#include "LocalDetailPatch.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace Raw::Bracketing {
bool CombineLocalDetail(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    std::vector<std::vector<Observation>>& groups,std::string& error) {
    try {
    const unsigned tile=data.sources[data.origin].frame.tileRawPixels,left=tx*tile,top=ty*tile;
    const unsigned width=std::min(tile,data.width-left),height=std::min(tile,data.height-top);
    static const LocalDetailTransform small(16),large(32);
    PreparedTileSampler sampler(data.directory,8);
    std::array<double,256> windows{};double windowSum=0,windowSquared=0;
    for(unsigned p=0;p<256;++p){windows[p]=LocalDetailWindow(p%16,p/16);windowSum+=windows[p];windowSquared+=windows[p]*windows[p];}
    struct NoiseUpdate {double sensorSd=0,modelSd=0,baselineSensor=0,baselineModel=0;};
    for(std::size_t g=0;g<groups.size();++g) {
        std::vector<std::size_t> sources;
        for(std::size_t f=0;f<data.sources.size();++f)if(data.sources[f].enabled&&data.sources[f].group==g)sources.push_back(f);
        if(sources.size()<3)continue;
        std::vector<NoiseUpdate> noise(width*height);
        // A global lattice gives native views and every tile layout the same
        // patches and summation order. Each synthesis footprint is 16 CFA
        // pixels; a 32-pixel analysis still corrects that central footprint.
        const unsigned firstX=std::max(32u,(left/16)*16),firstY=std::max(32u,(top/16)*16);
        for(unsigned cy=firstY;cy<top+height+16&&cy+20<data.height;cy+=16)
            for(unsigned cx=firstX;cx<left+width+16&&cx+20<data.width;cx+=16) {
                if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
                LocalDetailPatchResult patch;
                if(!EvaluateLocalDetailPatch(request,data,sources,sampler,cx,cy,small,patch,error))return false;
                if(!patch.accepted&&patch.mayExpand)
                    if(!EvaluateLocalDetailPatch(request,data,sources,sampler,cx,cy,large,patch,error))return false;
                if(!patch.accepted)continue;
                for(unsigned c=0;c<4;++c)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
                    const unsigned rx=cx-16+2*x+c%2,ry=cy-16+2*y+c/2;
                    if(rx<left||ry<top||rx>=left+width||ry>=top+height)continue;
                    const auto p=std::size_t(ry-top)*width+rx-left;const auto k=y*16+x;auto& sample=groups[g][p];
                    if(!sample.finite||sample.clipped||sample.support<=0)continue;
                    const double w=windows[k];sample.value+=w*patch.correction[c][k];
                    sample.support=std::min(sample.support,patch.captureSupport);
                    // Mean preservation and overlapping patches share inputs.
                    // Accumulate standard-deviation bounds, never independent
                    // patch variances or fictitious neighboring-frame support.
                    const double rowNorm=std::sqrt(1-2*w/windowSum+windowSquared/(windowSum*windowSum));
                    auto& v=noise[p];v.sensorSd+=w*rowNorm*std::sqrt(patch.measurementBound[c]);
                    v.modelSd+=w*rowNorm*std::sqrt(patch.uncertaintyBound[c]);
                    v.baselineSensor=std::max(v.baselineSensor,patch.baselineMeasurementBound[c]);
                    v.baselineModel=std::max(v.baselineModel,patch.baselineUncertaintyBound[c]);
                }
            }
        for(std::size_t p=0;p<noise.size();++p)if(noise[p].sensorSd>0||noise[p].modelSd>0) {
            auto& sample=groups[g][p];const auto& v=noise[p];
            sample.measurementVariance=std::pow(std::sqrt(std::max(sample.measurementVariance,v.baselineSensor))+v.sensorSd,2);
            sample.uncertaintyVariance=std::pow(std::sqrt(std::max(sample.uncertaintyVariance,v.baselineModel))+v.modelSd,2);
            sample.variance=sample.measurementVariance+sample.uncertaintyVariance;
        }
    }
    return true;
    } catch(const std::bad_alloc&) {
        error="The local detail working buffers could not be allocated.";return false;
    }
}
}
