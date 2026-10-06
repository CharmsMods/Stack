#include "Reconstruction.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

namespace Raw::Bracketing {
Preview SuperResolutionDetail(const BracketingResult& result,unsigned x,unsigned y,unsigned size,
    const std::function<bool()>& cancel,std::uint64_t memoryBudgetBytes) {
    if(!result.reconstructionInspection||!result.raw||!result.analysis)return {};
    const auto& cache=*result.reconstructionInspection;
    ProcessingRequest request;request.analysis=result.analysis;
    request.shouldCancel=cancel;request.memoryBudgetBytes=memoryBudgetBytes;
    std::string error;
    request.recipe=cache.recipe;
    auto native=RenderNativeDetail(request,x/cache.scale,y/cache.scale,std::max(16u,size/cache.scale));
    if(!native.width)return {};
    Preview out;out.metadata=native.metadata;
    out.width=native.width*cache.scale;out.height=native.height*cache.scale;
    const unsigned left=unsigned(native.sensorOriginX)*cache.scale,top=unsigned(native.sensorOriginY)*cache.scale;
    out.sensorStepX=out.sensorStepY=1.0/cache.scale;
    out.sensorOriginX=(left+.5)/cache.scale-.5;out.sensorOriginY=(top+.5)/cache.scale-.5;
    const auto count=std::size_t(out.width)*out.height,groups=std::size_t(cache.groups);
    out.resultRgb.resize(count*3);out.sourceRgb.resize(count*groups*3);
    out.guideEv.resize(count);out.contributions.resize(count*groups);out.requested.resize(count*groups);out.diagnostics.resize(count);
    out.originals=native.originals;
    const auto& raw=*result.raw;const auto& evidence=*raw.multiFrameMeasurementSidecars;
    std::map<std::pair<unsigned,unsigned>,std::vector<float>> masks;
    for(unsigned yy=0;yy<out.height;++yy) {
      if(cancel&&cancel())return {};
      for(unsigned xx=0;xx<out.width;++xx) {
        const auto p=std::size_t(yy)*out.width+xx,q=std::size_t(yy/cache.scale)*native.width+xx/cache.scale;
        const unsigned gx=left+xx,gy=top+yy;const auto index=std::size_t(gy)*raw.metadata.visibleWidth+gx;
        for(unsigned c=0;c<3;++c)out.resultRgb[p*3+c]=raw.linearFloatBuffer[index*3+c];
        std::copy_n(native.sourceRgb.begin()+q*groups*3,groups*3,out.sourceRgb.begin()+p*groups*3);
        out.guideEv[p]=native.guideEv[q];
        // The stored actual map is the exact average of evaluated RGB weights.
        const auto key=std::make_pair(gx/cache.tilePixels,gy/cache.tilePixels);
        auto found=masks.find(key);
        const unsigned tw=std::min(cache.tilePixels,unsigned(raw.metadata.visibleWidth)-key.first*cache.tilePixels);
        if(found==masks.end()) {
            const unsigned th=std::min(cache.tilePixels,unsigned(raw.metadata.visibleHeight)-key.second*cache.tilePixels);
            std::vector<float> values(std::size_t(tw)*th*groups);
            std::ifstream file(cache.directory/(std::to_string(key.first)+"-"+std::to_string(key.second)+".bin"),std::ios::binary);
            if(!file.read(reinterpret_cast<char*>(values.data()),std::streamsize(values.size()*sizeof(float))))
                throw std::runtime_error("Super-resolution inspection cache is missing or incomplete. Refresh the bracket.");
            const auto checksum=cache.checksums.find(key);
            if(checksum==cache.checksums.end()||checksum->second!=Stack::NodeMath::Sha256ContentIdentity(
                std::vector<std::string_view>{std::string_view(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float))}))
                throw std::runtime_error("Super-resolution inspection cache is damaged. Refresh the bracket.");
            found=masks.emplace(key,std::move(values)).first;
        }
        const auto maskIndex=(std::size_t(gy%cache.tilePixels)*tw+gx%cache.tilePixels)*groups;
        std::copy_n(found->second.begin()+maskIndex,groups,out.contributions.begin()+p*groups);
        for(std::size_t g=0;g<groups;++g)out.requested[p*groups+g]=native.requested[q*groups+g];
        out.diagnostics[p]=((*evidence.fallbackReason)[index]?Preview::Fallback:0)|
            (!(*evidence.validity)[index]&&!(*evidence.clipping)[index]?Preview::Invalid:0)|((*evidence.clipping)[index]?Preview::Unrecoverable:0)|
            ((*evidence.localRejection)[index]?Preview::AlignmentRejected:0)|
            ((*evidence.fallbackReason)[index]==std::uint8_t(MeasurementFallbackReason::ShortestExposure)?Preview::ShortestExposure:0)|
            ((*evidence.fallbackReason)[index]==std::uint8_t(MeasurementFallbackReason::FixedReference)?Preview::FixedReference:0);
      }
    }
    return out;
}
}
