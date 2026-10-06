#include "InspectionImage.h"
#include "Panorama/Panorama.h"
#include "ProcessingInternal.h"
#include "Raw/RawProcessingMath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Raw::Bracketing {
std::shared_ptr<const CapturePreview> RenderInspectionImage(const ProcessingRequest& request,
    const BracketingResult& result,InspectionImageKind kind,const std::string& id,unsigned group,unsigned maximumDimension) {
    if(result.panorama)return Panorama::Inspect(request,result,kind==InspectionImageKind::Capture?id:std::string(),maximumDimension);
    if(!request.analysis||!request.analysis->prepared||!maximumDimension)return {};
    if(request.shouldCancel&&request.shouldCancel())return {};
    const auto& data=*request.analysis->prepared;
    const auto found=std::find_if(data.sources.begin(),data.sources.end(),[&](const auto& s){return s.id==id;});
    if(kind==InspectionImageKind::Capture&&found==data.sources.end())return {};
    if(kind==InspectionImageKind::Group&&group>=request.recipe.groups.size())return {};
    const auto& source=kind==InspectionImageKind::Capture?*found:data.sources[data.origin];
    const bool linear=kind==InspectionImageKind::Result&&result.raw&&result.raw->reconstructedCameraRgb;
    const unsigned width=linear?result.raw->metadata.visibleWidth:data.width;
    const unsigned height=linear?result.raw->metadata.visibleHeight:data.height;
    const auto pixels=std::size_t(width)*height;
    const std::uint64_t limit=std::min<std::uint64_t>(512ull*1024*1024,request.memoryBudgetBytes/3);
    const auto tileSize=source.frame.tileRawPixels;
    const std::uint64_t reserve=std::uint64_t(tileSize)*tileSize*(request.recipe.groups.size()*sizeof(Observation)+24);
    const std::uint64_t mosaicBytes=linear?0:pixels*5;
    if(!pixels||mosaicBytes+reserve+1024*1024>=limit)throw std::runtime_error("Use native detail for this capture within the available memory.");
    const auto outputBudget=(limit-mosaicBytes-reserve)/17;
    const double scale=std::min({1.,double(maximumDimension)/std::max(width,height),std::sqrt(double(outputBudget)/pixels)});
    auto out=std::make_shared<CapturePreview>();
    out->width=std::max(1u,unsigned(width*scale));out->height=std::max(1u,unsigned(height*scale));
    out->exposureScale=kind==InspectionImageKind::Capture?source.scale:1;
    std::vector<float> mosaic(linear?0:pixels);std::vector<std::uint8_t> clipping(linear?0:pixels);
    if(!linear) {
        Mfd::DirectoryNormalizedTileCache cache(data.directory);
        for(unsigned ty=0;ty<source.frame.tileRows;++ty)for(unsigned tx=0;tx<source.frame.tileColumns;++tx) {
            if(request.shouldCancel&&request.shouldCancel())return {};
            Mfd::PreparedRawTile tile;std::string error;std::vector<std::vector<Observation>> groups;
            if(kind==InspectionImageKind::Group) {
                if(!ReadGroups(request,data,tx,ty,groups,tile,error))throw std::runtime_error(error);
            } else if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit)throw std::runtime_error(error);
            for(unsigned y=0;y<tile.extent.height;++y)for(unsigned x=0;x<tile.extent.width;++x) {
                const auto q=std::size_t(y)*tile.extent.width+x,p=(tile.originY+y)*width+tile.originX+x;
                if(kind==InspectionImageKind::Group) {
                    const auto& sample=groups[group][q];mosaic[p]=float(sample.support>0?sample.value:sample.fallback);clipping[p]=sample.clipped;
                } else if(kind==InspectionImageKind::Result) {
                    if(!result.raw||!result.raw->normalizedMosaicBuffer)return {};
                    mosaic[p]=result.raw->normalizedMosaicBuffer->at(p)*tile.comparisonGain[q];
                    const auto& sidecars=result.raw->multiFrameMeasurementSidecars;
                    clipping[p]=sidecars&&sidecars->clipping?sidecars->clipping->at(p):0;
                } else {
                    mosaic[p]=tile.normalizedMosaic[q]*tile.comparisonGain[q];
                    clipping[p]=Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::Saturated)||
                        Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::ExplicitDecoderClip);
                }
            }
        }
    }
    const auto count=std::size_t(out->width)*out->height;
    out->rgb.resize(count*3);out->clipped.resize(count);
    for(unsigned y=0;y<out->height;++y) {
        if(request.shouldCancel&&request.shouldCancel())return {};
        for(unsigned x=0;x<out->width;++x) {
            // Area reduction only when the device budget requires it. At
            // native size each displayed pixel is demosaiced at that sensor site.
            const unsigned left=std::uint64_t(x)*width/out->width,right=std::uint64_t(x+1)*width/out->width;
            const unsigned top=std::uint64_t(y)*height/out->height,bottom=std::uint64_t(y+1)*height/out->height;
            const auto p=std::size_t(y)*out->width+x;const float weight=1.f/((right-left)*(bottom-top));
            for(unsigned yy=top;yy<bottom;++yy)for(unsigned xx=left;xx<right;++xx) {
                const auto q=std::size_t(yy)*width+xx;
                const auto rgb=linear?std::array<float,3>{result.raw->linearFloatBuffer[q*3],result.raw->linearFloatBuffer[q*3+1],result.raw->linearFloatBuffer[q*3+2]}:
                    Processing::DemosaicMalvarHeCutlerAt(mosaic,width,height,source.frame.activeCfaPattern,xx,yy);
                for(unsigned c=0;c<3;++c)out->rgb[p*3+c]+=rgb[c]*weight;
                if(!linear)out->clipped[p]|=clipping[q];
            }
        }
    }
    return out;
}
}
