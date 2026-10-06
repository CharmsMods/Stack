#include "ProcessingInternal.h"
#include "HdrColorFusion.h"
#include "LocalDetail.h"
#include "Raw/RawProcessingMath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Raw::Bracketing {
Preview RenderNativeDetail(const ProcessingRequest& request,unsigned centerX,unsigned centerY,unsigned size) {
    if(!request.analysis||!request.analysis->prepared) return {};
    if(request.analysis->identity!=AnalysisIdentity(request)) return {};
    const auto& analysis=*request.analysis;const auto& data=*analysis.prepared;
    const unsigned width=std::min(size+8,data.width)&~1u,height=std::min(size+8,data.height)&~1u;
    const unsigned left=std::min(centerX>width/2?centerX-width/2:0u,data.width-width)&~1u;
    const unsigned top=std::min(centerY>height/2?centerY-height/2:0u,data.height-height)&~1u;
    const auto groups=request.recipe.groups.size(),pixels=static_cast<std::size_t>(width)*height;
    const auto tilePixels=static_cast<std::uint64_t>(data.sources[data.origin].frame.tileRawPixels)*data.sources[data.origin].frame.tileRawPixels;
    if(pixels*(groups*5+4+data.sources.size()*5)*sizeof(float)+tilePixels*(groups*(sizeof(Observation)+16)+48)+
       LocalDetailWorkingBytes(data.sources.size(),data.sources[data.origin].frame.tileRawPixels)>request.memoryBudgetBytes)
        throw std::runtime_error("Native detail exceeds the available memory budget.");
    std::vector<float> merged(pixels);std::vector<std::vector<float>> sources(groups,std::vector<float>(pixels));
    Preview out;out.width=width;out.height=height;out.sensorOriginX=left;out.sensorOriginY=top;
    out.metadata=data.sources[data.origin].metadata;
    out.metadata.cfaPattern=data.sources[data.origin].frame.activeCfaPattern;
    out.guideEv.resize(pixels);out.contributions.resize(pixels*groups);out.requested.resize(pixels*groups);
    out.diagnostics.resize(pixels);
    const unsigned tileSize=data.sources[data.origin].frame.tileRawPixels;
    for(unsigned ty=top/tileSize;ty<=(top+height-1)/tileSize;++ty) for(unsigned tx=left/tileSize;tx<=(left+width-1)/tileSize;++tx) {
        if(request.shouldCancel&&request.shouldCancel()) return {};
        std::vector<std::vector<Observation>> observations;Mfd::PreparedRawTile tile;std::string error;
        if(!ReadGroups(request,data,tx,ty,observations,tile,error)) throw std::runtime_error(error);
        ColorObservations colorInput;
        std::array<Pixel,4> color;
        for(unsigned y=std::max<unsigned>(top,tile.originY);y<std::min<unsigned>(top+height,tile.originY+tile.extent.height);++y)
        for(unsigned x=std::max<unsigned>(left,tile.originX);x<std::min<unsigned>(left+width,tile.originX+tile.extent.width);++x) {
            const auto i=(y-top)*width+x-left;
            const auto p=(y-tile.originY)*tile.extent.width+x-tile.originX;
            const float ev=analysis.guideEv[(y/2)*(data.width/2)+x/2];out.guideEv[i]=ev;
            auto shares=Evaluate(request.recipe.automatic?analysis.suggestion:request.recipe.knots,ev);
            // Rows revisit each cell, but both green sites use exactly the same decision.
            if((x&1u)==0) {
                const auto q=((y&~1u)-tile.originY)*tile.extent.width+x-tile.originX;
                GatherColorCell(observations,q,tile.extent.width,colorInput);
                color=BlendColor(colorInput,shares,request.recipe);
            }
            for(std::size_t g=0;g<groups;++g) {const auto& v=observations[g][p];sources[g][i]=static_cast<float>(v.support>0?v.value:v.fallback);out.requested[i*groups+g]=static_cast<float>(shares[g]);}
            const auto& pixel=color[(y%2)*2+x%2];merged[i]=static_cast<float>(pixel.value);
            out.diagnostics[i]=(pixel.fallback?Preview::Fallback:0)|
                (pixel.clipped?Preview::Unrecoverable:0)|
                (!pixel.valid&&!pixel.clipped?Preview::Invalid:0)|
                (pixel.localRejected?Preview::AlignmentRejected:0)|
                (pixel.fallbackReason==MeasurementFallbackReason::FixedReference?Preview::FixedReference:0)|
                (pixel.fallbackReason==MeasurementFallbackReason::ShortestExposure?Preview::ShortestExposure:0);
            for(std::size_t g=0;g<groups;++g) out.contributions[i*groups+g]=static_cast<float>(pixel.actual[g]);
        }
    }
    out.resultRgb.resize(pixels*3);out.sourceRgb.resize(pixels*groups*3);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const auto i=y*width+x;
        const auto rgb=Processing::DemosaicMalvarHeCutlerAt(merged,width,height,out.metadata.cfaPattern,x,y);
        for(unsigned c=0;c<3;++c) out.resultRgb[i*3+c]=rgb[c];
        for(std::size_t g=0;g<groups;++g) {
            const auto source=Processing::DemosaicMalvarHeCutlerAt(sources[g],width,height,out.metadata.cfaPattern,x,y);
            for(unsigned c=0;c<3;++c) out.sourceRgb[(i*groups+g)*3+c]=source[c];
        }
    }
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    std::map<std::string,std::shared_ptr<CapturePreview>> originals;
    for(const auto& source:data.sources) {
        if(request.shouldCancel&&request.shouldCancel()) return {};
        auto original=std::make_shared<CapturePreview>();original->width=width;original->height=height;original->exposureScale=source.scale;
        original->rgb.resize(pixels*3);original->clipped.resize(pixels);
        std::vector<float> mosaic(pixels);
        for(unsigned ty=top/tileSize;ty<=(top+height-1)/tileSize;++ty) for(unsigned tx=left/tileSize;tx<=(left+width-1)/tileSize;++tx) {
            if(request.shouldCancel&&request.shouldCancel()) return {};
            Mfd::PreparedRawTile tile;std::string error;
            if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit) throw std::runtime_error(error);
            for(unsigned y=std::max<unsigned>(top,tile.originY);y<std::min<unsigned>(top+height,tile.originY+tile.extent.height);++y)
            for(unsigned x=std::max<unsigned>(left,tile.originX);x<std::min<unsigned>(left+width,tile.originX+tile.extent.width);++x) {
                const auto i=(y-top)*width+x-left;const auto q=(y-tile.originY)*tile.extent.width+x-tile.originX;
                const float v=tile.normalizedMosaic[q]*tile.comparisonGain[q];mosaic[i]=std::isfinite(v)?v:0;
                original->clipped[i]=Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::Saturated)||Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::ExplicitDecoderClip);
            }
        }
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
            const auto rgb=Processing::DemosaicMalvarHeCutlerAt(mosaic,width,height,source.frame.activeCfaPattern,x,y);
            for(unsigned c=0;c<3;++c)original->rgb[(y*width+x)*3+c]=rgb[c];
        }
        originals[source.id]=std::move(original);
    }
    // Hide the demosaic halo so ROI edges use the same neighbors as the full image.
    const unsigned borderX=width>8?4:0,borderY=height>8?4:0;
    const unsigned croppedWidth=width-2*borderX,croppedHeight=height-2*borderY;
    const auto crop=[&](std::vector<float>& values,std::size_t channels) {
        std::vector<float> cropped(static_cast<std::size_t>(croppedWidth)*croppedHeight*channels);
        for(unsigned y=0;y<croppedHeight;++y) std::copy_n(values.begin()+((y+borderY)*width+borderX)*channels,croppedWidth*channels,cropped.begin()+y*croppedWidth*channels);
        values=std::move(cropped);
    };
    crop(out.resultRgb,3);crop(out.sourceRgb,groups*3);crop(out.guideEv,1);crop(out.contributions,groups);crop(out.requested,groups);
    std::vector<std::uint8_t> diagnostics(static_cast<std::size_t>(croppedWidth)*croppedHeight);
    for(unsigned y=0;y<croppedHeight;++y)std::copy_n(out.diagnostics.begin()+(y+borderY)*width+borderX,croppedWidth,diagnostics.begin()+y*croppedWidth);
    out.diagnostics=std::move(diagnostics);
    for(auto& [id,original]:originals) {
        crop(original->rgb,3);
        std::vector<std::uint8_t> clipped(static_cast<std::size_t>(croppedWidth)*croppedHeight);
        for(unsigned y=0;y<croppedHeight;++y)std::copy_n(original->clipped.begin()+(y+borderY)*width+borderX,croppedWidth,clipped.begin()+y*croppedWidth);
        original->clipped=std::move(clipped);original->width=croppedWidth;original->height=croppedHeight;
        out.originals[id]=original;
    }
    out.width=croppedWidth;out.height=croppedHeight;
    out.sensorOriginX=left+borderX;out.sensorOriginY=top+borderY;
    return out;
}
} // namespace Raw::Bracketing
