#include "Internal.h"
#include "Raw/RawLoader.h"
#include "Raw/RawProcessingMath.h"
#include "Raw/MultiFrameDenoise/NoiseModel.h"
#include "Persistence/RawProjectModel.h"
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <set>

namespace Raw::Bracketing::Panorama {
Prepared::~Prepared() {
    if(removeOnRelease&&!directory.empty()){std::error_code e;std::filesystem::remove_all(directory,e);}
}
namespace {
cv::Mat Upright(const cv::Mat& in,int orientation) {
    cv::Mat out;
    switch(orientation) {
    case 2:cv::flip(in,out,1);break;
    case 3:cv::rotate(in,out,cv::ROTATE_180);break;
    case 4:cv::flip(in,out,0);break;
    case 5:cv::transpose(in,out);break;
    case 6:cv::rotate(in,out,cv::ROTATE_90_CLOCKWISE);break;
    case 7:cv::transpose(in,out);cv::flip(out,out,-1);break;
    case 8:cv::rotate(in,out,cv::ROTATE_90_COUNTERCLOCKWISE);break;
    default:out=in;break;
    }
    return out;
}
void PreparePixels(const ProcessingRequest& request,RawImageData& raw,Capture& capture,
    Mfd::DirectoryNormalizedTileCache& cache,cv::Mat& pixels) {
    // The linear path also lets the diagnostic runner use published panorama
    // photographs without changing the application's RAW capture importer.
    if(!raw.metadata.mosaiced&&raw.metadata.linearChannels==3&&raw.reconstructedCameraRgb) {
        const auto w=raw.metadata.visibleWidth,h=raw.metadata.visibleHeight;
        if(w<=0||h<=0||raw.linearFloatBuffer.size()!=std::size_t(w)*h*3)
            throw std::runtime_error("Incomplete linear panorama capture.");
        pixels=cv::Mat(h,w,CV_32FC(PixelChannels),cv::Scalar::all(0));
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto* p=pixels.ptr<float>(y)+x*PixelChannels;
            const auto q=(std::size_t(y)*w+x)*3;
            for(unsigned c=0;c<3;++c)p[c]=raw.linearFloatBuffer[q+c];
            p[3]=1e-7f;p[4]=std::max({p[0],p[1],p[2]})>=.995f?1.f:0.f;
        }
        return;
    }
    Mfd::PreparationOptions options;options.shouldCancel=request.shouldCancel;
    auto prepared=Mfd::PrepareRawFrame(raw,options,cache);
    if(!prepared.success)throw std::runtime_error(prepared.message);
    const auto& frame=prepared.frame;
    const auto w=unsigned(frame.activeExtent.width),h=unsigned(frame.activeExtent.height);
    Mfd::NoiseResolutionOptions noiseOptions;noiseOptions.enableGenericLowConfidence=true;
    const double iso=std::max(100.0,double(raw.metadata.isoSpeed));
    for(auto& site:noiseOptions.genericLowConfidenceSites){site.shotScale=1e-4*iso/100;site.offsetVariance=1e-7*iso*iso/10000;}
    auto noise=Mfd::ResolveNoiseModel(raw.metadata,frame,&cache,noiseOptions);
    if(!noise.resolved)throw std::runtime_error(noise.message);
    capture.trustedNoise=noise.model.quality!=Mfd::NoiseModelQuality::GenericLowConfidence&&noise.model.quality!=Mfd::NoiseModelQuality::Unavailable;
    std::vector<float> mosaic(std::size_t(w)*h),variance(mosaic.size());
    std::vector<std::uint8_t> clipping(mosaic.size());
    Mfd::CfaLayout cfa;Mfd::CfaLayout::TryCreate(frame.activeCfaPattern,cfa);
    for(unsigned ty=0;ty<frame.tileRows;++ty)for(unsigned tx=0;tx<frame.tileColumns;++tx) {
        CheckCanceled(request);Mfd::PreparedRawTile tile;std::string error;
        if(Mfd::ReadPreparedTile(frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit)throw std::runtime_error(error);
        for(unsigned y=0;y<tile.extent.height;++y)for(unsigned x=0;x<tile.extent.width;++x) {
            const auto q=std::size_t(y)*tile.extent.width+x,p=(tile.originY+y)*w+tile.originX+x;
            const float gain=tile.comparisonGain[q],v=tile.normalizedMosaic[q];
            if(!std::isfinite(v)||!std::isfinite(gain))throw std::runtime_error("Non-finite panorama sensor sample.");
            mosaic[p]=v*gain;
            const auto& profile=noise.model.sites[std::size_t(cfa.SiteAt(tile.originX+x,tile.originY+y))];
            variance[p]=float((profile.shotScale*std::max(0.f,v)+profile.offsetVariance+profile.quantizationVariance)*gain*gain);
            clipping[p]=Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::Saturated)||
                Mfd::HasSampleFlag(tile.sampleFlags[q],Mfd::PreparedSampleFlag::ExplicitDecoderClip);
        }
    }
    pixels=cv::Mat(int(h),int(w),CV_32FC(PixelChannels));
    for(unsigned y=0;y<h;++y) {
        CheckCanceled(request);
        for(unsigned x=0;x<w;++x) {
            const auto rgb=Processing::DemosaicMalvarHeCutlerAt(mosaic,w,h,frame.activeCfaPattern,x,y);
            auto* p=pixels.ptr<float>(int(y))+x*PixelChannels;
            for(unsigned c=0;c<3;++c)p[c]=rgb[c];
            // Conservative neighborhood variance, including demosaic correlations.
            float var=0,clip=0;
            for(int yy=std::max(0,int(y)-2);yy<=std::min(int(h)-1,int(y)+2);++yy)
                for(int xx=std::max(0,int(x)-2);xx<=std::min(int(w)-1,int(x)+2);++xx) {
                    var=std::max(var,variance[std::size_t(yy)*w+xx]);clip=std::max(clip,float(clipping[std::size_t(yy)*w+xx]));
                }
            p[3]=var*2;p[4]=clip;
        }
    }
}
}
std::shared_ptr<Prepared> Prepare(const ProcessingRequest& request) {
    auto data=std::make_shared<Prepared>();data->directory=request.cacheDirectory/"panorama";
    data->removeOnRelease=request.removeCacheOnRelease;
    std::filesystem::create_directories(data->directory);
    Mfd::DirectoryNormalizedTileCache cache(data->directory/"sensor");
    std::vector<Source> enabled;
    for(const auto& group:request.recipe.groups)if(group.enabled)for(const auto& frame:group.frames)if(frame.enabled) {
        auto s=std::find_if(request.sources.begin(),request.sources.end(),[&](const auto& source){return source.frameId==frame.id;});
        if(s==request.sources.end())throw std::runtime_error("A selected panorama capture is missing.");
        enabled.push_back(*s);
    }
    if(enabled.size()<2)throw std::runtime_error("Select at least two overlapping captures for a panorama.");
    std::set<std::string> hashes;
    for(const auto& source:enabled) {
        Progress(request,ProcessingStage::Preparing,.02+.20*data->captures.size()/enabled.size(),unsigned(data->captures.size()),unsigned(enabled.size()),source.frameId);
        if(!hashes.insert(source.sha256).second)throw std::runtime_error("Duplicate panorama captures cannot provide independent measurements.");
        RawImageData raw;RawMetadata metadata;
        if(!request.decode&&RawLoader::LoadMetadata(source.path.string(),metadata)&&
           std::uint64_t(std::max(0,metadata.rawWidth))*std::max(0,metadata.rawHeight)>request.memoryBudgetBytes/96)
            throw std::runtime_error("Preparing this panorama capture exceeds available memory.");
        const bool loaded=request.decode?request.decode(source.path,raw,request.shouldCancel):RawLoader::LoadFile(source.path.string(),raw,request.shouldCancel);
        if(!loaded)throw std::runtime_error("Could not decode "+source.displayName+": "+raw.metadata.error);
        if(raw.metadata.sourceContentSha256!=source.sha256||raw.metadata.sourceByteSize!=source.bytes)
            throw std::runtime_error("Panorama source content changed: "+source.displayName);
        if(std::uint64_t(raw.metadata.rawWidth)*raw.metadata.rawHeight>request.memoryBudgetBytes/96)
            throw std::runtime_error("Preparing this panorama capture exceeds available memory.");
        if(!data->captures.empty()) {
            const auto& previous=data->captures.front().metadata;
            if(previous.cameraMake!=raw.metadata.cameraMake||previous.cameraModel!=raw.metadata.cameraModel||
                (!previous.lensModel.empty()&&!raw.metadata.lensModel.empty()&&previous.lensModel!=raw.metadata.lensModel))
                throw std::runtime_error("Use captures from the same camera and lens for this panorama: "+source.displayName);
        }
        Capture capture;capture.source=source;capture.metadata=raw.metadata;
        cv::Mat sensor;PreparePixels(request,raw,capture,cache,sensor);
        cv::Mat pixels=Upright(sensor,ResolveOrientation(request.recipe,raw.metadata.orientation));
        capture.width=unsigned(pixels.cols);capture.height=unsigned(pixels.rows);
        capture.pixels=data->directory/(std::to_string(data->captures.size())+".rgb");
        std::ofstream out(capture.pixels,std::ios::binary|std::ios::trunc);
        for(int y=0;y<pixels.rows;++y){CheckCanceled(request);out.write(reinterpret_cast<const char*>(pixels.ptr<float>(y)),std::streamsize(pixels.cols)*PixelChannels*sizeof(float));}
        out.close();if(!out.good())throw std::runtime_error("Could not cache panorama pixels.");
        const double proxyPixels=std::min(1500000.0,double(request.memoryBudgetBytes)/std::max<std::size_t>(1,enabled.size())/160);
        capture.proxyScale=std::min(1.0,std::sqrt(proxyPixels/(double(pixels.cols)*pixels.rows)));
        // OpenCV area resampling does not support five-channel images at
        // arbitrary reductions. Resample the required channels independently.
        cv::Mat small;
        for(int channel:{0,1,2,4}) {
            cv::Mat plane,reduced;cv::extractChannel(pixels,plane,channel);
            cv::resize(plane,reduced,{},capture.proxyScale,capture.proxyScale,cv::INTER_AREA);
            if(small.empty())small=cv::Mat(reduced.size(),CV_32FC(PixelChannels),cv::Scalar::all(0));
            cv::insertChannel(reduced,small,channel);
        }
        capture.proxy=cv::Mat(small.size(),CV_32FC3);capture.clipping=cv::Mat(small.size(),CV_8U);
        cv::Mat luminance(small.size(),CV_32F);double mean=0;
        for(int y=0;y<small.rows;++y)for(int x=0;x<small.cols;++x) {
            const auto* p=small.ptr<float>(y)+x*PixelChannels;
            capture.proxy.at<cv::Vec3f>(y,x)={p[0],p[1],p[2]};capture.clipping.at<unsigned char>(y,x)=p[4]>.01f?255:0;
            const float l=std::max(0.f,(p[0]+2*p[1]+p[2])*.25f);luminance.at<float>(y,x)=l;mean+=l;
        }
        mean=std::max(1e-6,mean/small.total());capture.guide=cv::Mat(small.size(),CV_8U);
        for(int y=0;y<small.rows;++y)for(int x=0;x<small.cols;++x)
            capture.guide.at<unsigned char>(y,x)=cv::saturate_cast<unsigned char>(255*std::pow(luminance.at<float>(y,x)/(luminance.at<float>(y,x)+mean),.5));
        if(source.frameId==request.recipe.originFrameId)data->reference=unsigned(data->captures.size());
        data->captures.push_back(std::move(capture));
    }
    return data;
}
}
