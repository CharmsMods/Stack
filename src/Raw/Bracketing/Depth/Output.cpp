#include "Depth.h"
#include "../ProcessingInternal.h"
#include "Raw/RawProcessingMath.h"
#include <algorithm>

namespace Raw::Bracketing::Depth {
Preview ReadResultDetail(const ProcessingRequest& request,const BracketingResult& result,unsigned x,unsigned y,unsigned size) {
    auto preview=RenderNativeDetail(request,x,y,size);
    if(!result.raw||!result.raw->reconstructedCameraRgb)return preview;
    const auto& raw=*result.raw;
    for(unsigned yy=0;yy<preview.height;++yy) {
        if(request.shouldCancel&&request.shouldCancel())return {};
        const auto offset=(std::size_t(preview.sensorOriginY+yy)*raw.metadata.visibleWidth+unsigned(preview.sensorOriginX))*3;
        std::copy_n(raw.linearFloatBuffer.begin()+offset,std::size_t(preview.width)*3,
            preview.resultRgb.begin()+std::size_t(yy)*preview.width*3);
    }
    return preview;
}
bool Finish(const ProcessingRequest& request,BracketingResult& result,std::string& error) {
    if(!result.raw||!result.analysis||!result.analysis->depth)return true;
    const auto depth=result.analysis->depth;result.raw->reconstructionDepth=depth;
    if(!depth->Available()) {result.message="Bracket ready. "+depth->message;return true;}
    if(request.recipe.depth.blurRadius<=0)return true;
    const auto& data=*result.analysis->prepared;const auto& source=data.sources[data.origin];
    const unsigned width=result.raw->metadata.visibleWidth,height=result.raw->metadata.visibleHeight;
    const auto count=std::size_t(width)*height;
    // Reserve simultaneous sharp output, measurement maps and both RGB
    // rasters, plus resident correspondence evidence retained by the analysis.
    std::uint64_t retained=result.analysis->guideEv.capacity()*sizeof(float);
    for(const auto& capture:data.sources) {
        if(capture.reliability)for(const auto& tile:capture.reliability->tiles)
            retained+=tile.uint16Values.capacity()*sizeof(std::uint16_t)+
                tile.floatValues.capacity()*sizeof(float)+tile.rejectionBits.capacity()*sizeof(std::uint16_t);
        if(capture.localMotion)retained+=capture.localMotion->nodes.capacity()*sizeof(Mfd::MotionNode);
        if(capture.original)retained+=capture.original->rgb.capacity()*sizeof(float)+capture.original->clipped.capacity();
    }
    retained+=result.preview.samples.capacity()*sizeof(Preview::Sample);
    if(count*64ull+retained+64ull*1024*1024>request.memoryBudgetBytes) {
        error="Depth-of-field output exceeds the processing memory budget.";return false;
    }
    if(request.reportProgress)request.reportProgress(.98,"Rendering depth of field");
    auto raw=result.raw;
    if(!raw->reconstructedCameraRgb) {
        auto converted=std::make_shared<RawImageData>();converted->metadata=raw->metadata;
        converted->contentIdentity=raw->contentIdentity;converted->contentIdentityHash=raw->contentIdentityHash;
        converted->reconstructionDepth=depth;converted->reconstructedCameraRgb=true;
        auto& metadata=converted->metadata;
        metadata.pixelLayout=RawPixelLayout::LinearRgb;metadata.mosaiced=false;metadata.cfaPattern=CfaPattern::Unknown;
        metadata.linearChannels=3;metadata.linearSampleFormat=RawSampleFormat::Float32;
        metadata.dngGainMaps.clear();metadata.dngGainMapCount=0;metadata.dngLinearizationTable.clear();
        metadata.dngBlackLevelValues.clear();metadata.dngBlackLevelDeltaH.clear();metadata.dngBlackLevelDeltaV.clear();
        metadata.blackLevel=0;metadata.perChannelBlack={0,0,0,0};metadata.whiteLevel=1;metadata.sourceByteSize=count*3*sizeof(float);
        if(!DemosaicOutputGpu(request,data,*raw->normalizedMosaicBuffer,converted->linearFloatBuffer,error))return false;
        raw=std::move(converted);
    }
    std::vector<float> blurred;
    if(!RenderDefocusGpu(raw->linearFloatBuffer,width,height,*depth,request.recipe.depth,
        double(data.width)/width*.5-.5,double(data.height)/height*.5-.5,
        double(data.width)/width,double(data.height)/height,blurred,request.executeOpenGlTask,request.shouldCancel,error))return false;
    raw->linearFloatBuffer=std::move(blurred);
    // Measurement sidecars describe sharp sensor fusion. They must not be
    // presented as independent per-pixel noise after spatial defocus filtering.
    raw->multiFrameMeasurementSidecars.reset();
    raw->metadata.hasDngNoiseProfile=false;raw->metadata.dngNoiseProfile.clear();
    raw->metadata.hasDngBaselineNoise=false;
    result.raw=std::move(raw);
    if(!ResizeOutputGpu(request,result.raw->linearFloatBuffer,width,height,result.preview,error))return false;
    result.message="Bracket ready with depth of field. "+depth->message;return true;
}
}
