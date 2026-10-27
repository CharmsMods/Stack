#include "ResultDetail.h"
#include "NativeDetailRegion.h"
#include "ProcessingInternal.h"
#include "PreparedRegion.h"
#include "Raw/RawProcessingMath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Raw::Bracketing {
Preview ReadCompletedResultDetail(const ProcessingRequest& request,const BracketingResult& result,
    unsigned x,unsigned y,unsigned size) {
    if(!result.raw||!request.analysis||!request.analysis->prepared||!size)return {};
    if(request.shouldCancel&&request.shouldCancel())return {};
    const auto& raw=*result.raw;const auto& data=*request.analysis->prepared;
    if(data.origin>=data.sources.size()||raw.metadata.visibleWidth!=data.width||
       raw.metadata.visibleHeight!=data.height)throw std::runtime_error("Saved native result dimensions do not match the prepared reference.");
    const auto& origin=data.sources[data.origin];
    const auto r=DetailInternal::ChooseRegion(data.width,data.height,x,y,size);
    if(!r.width||!r.height)return {};
    const auto pixels=std::size_t(data.width)*data.height;
    const auto count=std::size_t(r.OutputWidth())*r.OutputHeight();
    const auto regionCount=std::size_t(r.width)*r.height;
    std::uint64_t largestTile=0;
    for(const auto& source:data.sources)largestTile=std::max(largestTile,
        std::uint64_t(source.frame.tileRawPixels)*source.frame.tileRawPixels);
    // The sampler retains four tiles and temporarily reads one more; all
    // image-sized scratch allocations below are bounded by this halo ROI.
    const long double needed=static_cast<long double>(regionCount)*26+
        static_cast<long double>(count)*(13*static_cast<long double>(data.sources.size())+13)+
        static_cast<long double>(largestTile)*45+1024*1024;
    if(needed>request.memoryBudgetBytes)throw std::runtime_error("Saved native detail exceeds the available memory budget.");
    Preview out;out.width=r.OutputWidth();out.height=r.OutputHeight();
    out.sensorOriginX=r.OutputLeft();out.sensorOriginY=r.OutputTop();out.metadata=raw.metadata;
    out.resultRgb.resize(count*3);
    if(raw.reconstructedCameraRgb) {
        if(raw.linearFloatBuffer.size()!=pixels*3)throw std::runtime_error("Saved native RGB pixels are incomplete.");
        for(unsigned row=0;row<out.height;++row) {
            if(request.shouldCancel&&request.shouldCancel())return {};
            const auto offset=(std::size_t(r.OutputTop()+row)*data.width+r.OutputLeft())*3;
            std::copy_n(raw.linearFloatBuffer.begin()+offset,std::size_t(out.width)*3,
                out.resultRgb.begin()+std::size_t(row)*out.width*3);
        }
    } else {
        if(!raw.normalizedMosaicBuffer||raw.normalizedMosaicBuffer->size()!=pixels||
           raw.normalizedMosaicInputContract!=NormalizedMosaicInputContract::BracketingPreGain||
           raw.metadata.cfaPattern!=origin.frame.activeCfaPattern)
            throw std::runtime_error("Saved native Bayer pixels do not match their preparation contract.");
        PreparedTileSampler sampler(data.directory,4);Mfd::PreparedRawTile tile;std::string error;
        if(!ReadPreparedRegion(origin.frame,sampler,r.left,r.top,r.width,r.height,tile,error))throw std::runtime_error(error);
        std::vector<float> mosaic(regionCount);
        for(unsigned row=0;row<r.height;++row) {
            if(request.shouldCancel&&request.shouldCancel())return {};
            for(unsigned col=0;col<r.width;++col) {
                const auto q=std::size_t(row)*r.width+col,p=std::size_t(r.top+row)*data.width+r.left+col;
                const float gain=tile.comparisonGain[q];
                mosaic[q]=(*raw.normalizedMosaicBuffer)[p]*gain;
                if(!std::isfinite(gain)||gain<=0||!std::isfinite(mosaic[q]))
                    throw std::runtime_error("Saved native Bayer calibration contains an invalid value.");
            }
        }
        for(unsigned row=0;row<out.height;++row) {
            if(request.shouldCancel&&request.shouldCancel())return {};
            for(unsigned col=0;col<out.width;++col) {
                const auto rgb=Processing::DemosaicMalvarHeCutlerAt(mosaic,r.width,r.height,
                    origin.frame.activeCfaPattern,col+r.borderX,row+r.borderY);
                std::copy(rgb.begin(),rgb.end(),out.resultRgb.begin()+(std::size_t(row)*out.width+col)*3);
            }
        }
    }
    const auto& evidence=raw.multiFrameMeasurementSidecars;
    // A partial set cannot establish that unrepresented conditions are absent.
    if(evidence&&evidence->validity&&evidence->clipping&&evidence->localRejection&&evidence->fallbackReason&&
       evidence->validity->size()==pixels&&evidence->clipping->size()==pixels&&
       evidence->localRejection->size()==pixels&&evidence->fallbackReason->size()==pixels) {
        out.diagnostics.resize(count);
        for(unsigned row=0;row<out.height;++row) {
            if(request.shouldCancel&&request.shouldCancel())return {};
            for(unsigned col=0;col<out.width;++col) {
                const auto p=std::size_t(r.OutputTop()+row)*data.width+r.OutputLeft()+col;
                out.diagnostics[std::size_t(row)*out.width+col]=
                    ((*evidence->fallbackReason)[p]?Preview::Fallback:0)|
                    (!(*evidence->validity)[p]&&!(*evidence->clipping)[p]?Preview::Invalid:0)|
                    ((*evidence->clipping)[p]?Preview::Unrecoverable:0)|
                    ((*evidence->localRejection)[p]?Preview::AlignmentRejected:0)|
                    ((*evidence->fallbackReason)[p]==std::uint8_t(MeasurementFallbackReason::FixedReference)?Preview::FixedReference:0)|
                    ((*evidence->fallbackReason)[p]==std::uint8_t(MeasurementFallbackReason::ShortestExposure)?Preview::ShortestExposure:0);
            }
        }
    }
    std::string error;
    if(!DetailInternal::ReadOriginals(request,data,r,out.originals,error)) {
        if(error=="Canceled")return {};
        throw std::runtime_error(error);
    }
    return out;
}
}
