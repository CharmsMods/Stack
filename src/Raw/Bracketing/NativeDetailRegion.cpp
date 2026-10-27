#include "NativeDetailRegion.h"
#include "ProcessingInternal.h"
#include "PreparedRegion.h"
#include "Raw/RawProcessingMath.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing::DetailInternal {
Region ChooseRegion(unsigned width,unsigned height,unsigned x,unsigned y,unsigned size) {
    if(!size)return {};
    // Bayer framing is even. At least two requested pixels keep a full
    // four-pixel halo on each side unless the source itself is smaller.
    size=std::max(2u,size);
    Region r;
    r.width=unsigned(std::min<std::uint64_t>(std::uint64_t(size)+8,width))&~1u;
    r.height=unsigned(std::min<std::uint64_t>(std::uint64_t(size)+8,height))&~1u;
    if(!r.width||!r.height)return {};
    r.left=std::min(x>r.width/2?x-r.width/2:0u,width-r.width)&~1u;
    r.top=std::min(y>r.height/2?y-r.height/2:0u,height-r.height)&~1u;
    r.borderX=r.width>8?4:0;r.borderY=r.height>8?4:0;
    return r;
}
bool ReadOriginals(const ProcessingRequest& request,const PreparedDataset& data,const Region& r,
    std::map<std::string,std::shared_ptr<const CapturePreview>>& originals,std::string& error) {
    const auto count=std::size_t(r.OutputWidth())*r.OutputHeight();
    PreparedTileSampler sampler(data.directory,4);
    for(const auto& source:data.sources) {
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        Mfd::PreparedRawTile tile;
        if(!ReadPreparedRegion(source.frame,sampler,r.left,r.top,r.width,r.height,tile,error))return false;
        auto original=std::make_shared<CapturePreview>();
        original->width=r.OutputWidth();original->height=r.OutputHeight();original->exposureScale=source.scale;
        original->rgb.resize(count*3);original->clipped.resize(count);
        std::vector<float> mosaic(tile.normalizedMosaic.size());
        for(unsigned y=0;y<r.height;++y) {
            if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
            for(unsigned x=0;x<r.width;++x) {
                const auto p=std::size_t(y)*r.width+x;
                const float value=tile.normalizedMosaic[p]*tile.comparisonGain[p];
                mosaic[p]=std::isfinite(value)?value:0;
            }
        }
        for(unsigned y=0;y<original->height;++y) {
            if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
            for(unsigned x=0;x<original->width;++x) {
                const auto q=std::size_t(y+r.borderY)*r.width+x+r.borderX,p=std::size_t(y)*original->width+x;
                const auto rgb=Processing::DemosaicMalvarHeCutlerAt(mosaic,r.width,r.height,
                    source.frame.activeCfaPattern,x+r.borderX,y+r.borderY);
                std::copy(rgb.begin(),rgb.end(),original->rgb.begin()+p*3);
                original->clipped[p]=Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::Saturated)||
                    Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::ExplicitDecoderClip);
            }
        }
        originals[source.id]=std::move(original);
    }
    return true;
}
}
