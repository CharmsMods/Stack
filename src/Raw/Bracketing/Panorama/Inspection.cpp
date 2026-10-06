#include "Internal.h"
#include <fstream>
#include <cmath>

namespace Raw::Bracketing::Panorama {
std::shared_ptr<const CapturePreview> Inspect(const ProcessingRequest& request,const BracketingResult& result,const std::string& frame,unsigned maximum) {
    const Capture* capture=nullptr;
    if(!frame.empty()) {
        if(!request.analysis||!request.analysis->panorama)return {};
        for(const auto& source:request.analysis->panorama->captures)if(source.source.frameId==frame){capture=&source;break;}
        if(!capture)return {};
    }
    if(!capture&&!result.raw)return {};
    const unsigned width=capture?capture->width:unsigned(result.raw->metadata.visibleWidth),height=capture?capture->height:unsigned(result.raw->metadata.visibleHeight);
    const double scale=std::min({1.,double(maximum)/std::max(width,height),std::sqrt(double(request.memoryBudgetBytes)/64/(double(width)*height))});
    if(!(scale>0))return {};
    auto out=std::make_shared<CapturePreview>();out->width=std::max(1u,unsigned(width*scale));out->height=std::max(1u,unsigned(height*scale));
    const auto count=std::size_t(out->width)*out->height;out->rgb.resize(count*3);out->coverage.resize(count);out->clipped.resize(count);
    std::ifstream file;std::vector<float> row;
    if(capture){file.open(capture->pixels,std::ios::binary);if(!file)throw std::runtime_error("Panorama source inspection cache is unavailable.");row.resize(std::size_t(width)*PixelChannels);}
    for(unsigned y=0;y<out->height;++y) {
        CheckCanceled(request);const unsigned top=std::uint64_t(y)*height/out->height,bottom=std::uint64_t(y+1)*height/out->height;
        std::vector<double> sums(std::size_t(out->width)*4,0);
        for(unsigned yy=top;yy<bottom;++yy) {
            if(capture){file.seekg(std::uint64_t(yy)*width*PixelChannels*sizeof(float));if(!file.read(reinterpret_cast<char*>(row.data()),row.size()*sizeof(float)))throw std::runtime_error("Panorama source inspection pixels are incomplete.");}
            for(unsigned x=0;x<out->width;++x)for(unsigned xx=std::uint64_t(x)*width/out->width;xx<std::uint64_t(x+1)*width/out->width;++xx) {
                const auto p=std::size_t(yy)*width+xx;const float a=capture?1:result.raw->outputCoverage?(*result.raw->outputCoverage)[p]:1;
                const float* rgb=capture?row.data()+xx*PixelChannels:result.raw->linearFloatBuffer.data()+p*3;
                for(int c=0;c<3;++c)sums[x*4+c]+=rgb[c]*a;sums[x*4+3]+=a;
                const bool clipped=capture?row[xx*PixelChannels+4]>.01f:
                    result.raw->multiFrameMeasurementSidecars&&result.raw->multiFrameMeasurementSidecars->clipping&&
                    (*result.raw->multiFrameMeasurementSidecars->clipping)[p]!=0;
                out->clipped[std::size_t(y)*out->width+x]|=std::uint8_t(clipped);
            }
        }
        for(unsigned x=0;x<out->width;++x){const auto p=std::size_t(y)*out->width+x;const double a=sums[x*4+3];
            for(int c=0;c<3;++c)out->rgb[p*3+c]=float(sums[x*4+c]/std::max(1e-9,a));
            out->coverage[p]=float(a/((bottom-top)*(std::uint64_t(x+1)*width/out->width-std::uint64_t(x)*width/out->width)));}
    }
    if(capture&&result.panorama)for(const auto& c:result.panorama->cameras)if(c.frameId==frame)out->exposureScale=c.gain;
    return out;
}
}
