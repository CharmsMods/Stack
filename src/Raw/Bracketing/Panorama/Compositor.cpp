#include "Internal.h"
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <cmath>

namespace Raw::Bracketing::Panorama {
namespace {
struct Warped {cv::Mat rgb,variance,clipping,mask;};
Warped ReadWarped(const ProcessingRequest& request,const Capture& source,const Camera& camera,const Layout& layout,const cv::Rect& tile) {
    Warped out;out.rgb=cv::Mat::zeros(tile.size(),CV_32FC3);out.variance=cv::Mat::zeros(tile.size(),CV_32F);
    out.clipping=cv::Mat::zeros(tile.size(),CV_32F);out.mask=cv::Mat::zeros(tile.size(),CV_32F);
    cv::Mat mx(tile.size(),CV_32F,cv::Scalar(-1)),my(tile.size(),CV_32F,cv::Scalar(-1));
    int left=int(source.width),right=-1,top=int(source.height),bottom=-1;
    for(int y=0;y<tile.height;++y){CheckCanceled(request);for(int x=0;x<tile.width;++x) {
        cv::Point2d p;if(!Unproject(camera,layout,x+tile.x,y+tile.y,p))continue;
        mx.at<float>(y,x)=float(p.x);my.at<float>(y,x)=float(p.y);out.mask.at<float>(y,x)=1;
        left=std::min(left,int(std::floor(p.x)));right=std::max(right,int(std::ceil(p.x)));
        top=std::min(top,int(std::floor(p.y)));bottom=std::max(bottom,int(std::ceil(p.y)));
    }}
    if(right<left)return out;
    const int width=right-left+1,height=bottom-top+1;
    if(std::uint64_t(width)*height*PixelChannels*sizeof(float)>request.memoryBudgetBytes/8)
        throw std::runtime_error("A panorama warp requires too much memory. Reduce the panorama output size or use Spherical projection.");
    cv::Mat block(height,width,CV_32FC(PixelChannels));
    std::ifstream file(source.pixels,std::ios::binary);if(!file)throw std::runtime_error("The panorama pixel cache is unavailable. Process the captures again.");
    for(int y=0;y<height;++y) {
        CheckCanceled(request);file.seekg((std::uint64_t(y+top)*source.width+left)*PixelChannels*sizeof(float));
        if(!file.read(reinterpret_cast<char*>(block.ptr<float>(y)),std::streamsize(width)*PixelChannels*sizeof(float)))throw std::runtime_error("Incomplete panorama pixel cache.");
    }
    mx-=left;my-=top;cv::Mat sampled;cv::remap(block,sampled,mx,my,cv::INTER_LINEAR,cv::BORDER_REPLICATE);
    for(int y=0;y<tile.height;++y)for(int x=0;x<tile.width;++x)if(out.mask.at<float>(y,x)>0) {
        const auto* p=sampled.ptr<float>(y)+x*PixelChannels;
        out.rgb.at<cv::Vec3f>(y,x)={float(p[0]*camera.gain),float(p[1]*camera.gain),float(p[2]*camera.gain)};
        out.variance.at<float>(y,x)=float(p[3]*camera.gain*camera.gain);
        out.clipping.at<float>(y,x)=p[4];
    }
    return out;
}
// Normalize every level against geometric support. Black outside the panorama
// never enters the low-frequency color or creates a dark edge.
cv::Mat MultiBand(const std::vector<Warped>& images,const std::vector<cv::Mat>& masks) {
    constexpr unsigned levels=5;
    std::vector<cv::Mat> sums(levels),weights(levels);
    for(std::size_t i=0;i<images.size();++i) {
        std::vector<cv::Mat> rgb(levels),coverage(levels),blend(levels);
        rgb[0]=images[i].rgb;coverage[0]=images[i].mask;blend[0]=masks[i];
        for(unsigned l=1;l<levels;++l) {
            cv::Mat numerator=rgb[l-1].clone();
            for(int y=0;y<numerator.rows;++y)for(int x=0;x<numerator.cols;++x)numerator.at<cv::Vec3f>(y,x)*=coverage[l-1].at<float>(y,x);
            cv::pyrDown(numerator,rgb[l]);cv::pyrDown(coverage[l-1],coverage[l]);cv::pyrDown(blend[l-1],blend[l]);
            for(int y=0;y<rgb[l].rows;++y)for(int x=0;x<rgb[l].cols;++x)rgb[l].at<cv::Vec3f>(y,x)/=std::max(1e-8f,coverage[l].at<float>(y,x));
        }
        for(unsigned l=0;l<levels;++l) {
            cv::Mat band=rgb[l].clone();
            if(l+1<levels){cv::Mat up;cv::pyrUp(rgb[l+1],up,rgb[l].size());band-=up;}
            if(sums[l].empty()){sums[l]=cv::Mat::zeros(band.size(),CV_32FC3);weights[l]=cv::Mat::zeros(band.size(),CV_32F);}
            for(int y=0;y<band.rows;++y)for(int x=0;x<band.cols;++x) {
                const float w=blend[l].at<float>(y,x)*coverage[l].at<float>(y,x);
                sums[l].at<cv::Vec3f>(y,x)+=band.at<cv::Vec3f>(y,x)*w;weights[l].at<float>(y,x)+=w;
            }
        }
    }
    for(unsigned l=0;l<levels;++l)for(int y=0;y<sums[l].rows;++y)for(int x=0;x<sums[l].cols;++x)
        sums[l].at<cv::Vec3f>(y,x)/=std::max(1e-8f,weights[l].at<float>(y,x));
    cv::Mat result=sums.back();for(int l=int(levels)-2;l>=0;--l){cv::Mat up;cv::pyrUp(result,up,sums[l].size());result=up+sums[l];}
    return result;
}
}
void Composite(const ProcessingRequest& request,const Prepared& data,const Seams& seams,BracketingResult& result) {
    const auto layoutOwner=result.panorama;const auto& layout=*layoutOwner;const auto n=data.captures.size();
    auto raw=result.raw;const std::size_t pixels=std::size_t(layout.width)*layout.height;
    raw->linearFloatBuffer.resize(pixels*3);
    auto coverage=std::make_shared<std::vector<float>>(pixels,0);
    auto owners=std::make_shared<std::vector<std::uint16_t>>(pixels,65535);
    auto variance=std::make_shared<std::vector<float>>(pixels,0),support=std::make_shared<std::vector<float>>(pixels,0);
    auto clipping=std::make_shared<std::vector<std::uint8_t>>(pixels,0);
    auto maps=std::make_shared<RawImageData::MultiFrameMeasurementSidecars>();
    for(const auto& source:data.captures)maps->originalFrameIds.push_back(source.source.frameId);
    maps->variance=variance;maps->effectiveSupport=support;maps->clipping=clipping;maps->evidenceIdentitySha256=result.identity;
    raw->multiFrameMeasurementSidecars=maps;raw->outputCoverage=coverage;
    auto published=std::make_shared<Layout>(layout);published->ownership=owners;result.panorama=published;
    constexpr int halo=64;
    int tileSide=256;
    const auto tileAllowance=std::max<std::uint64_t>(1,request.memoryBudgetBytes/4);
    while(tileSide>64&&std::uint64_t(tileSide+2*halo)*(tileSide+2*halo)*(40*n+160)>tileAllowance)tileSide/=2;
    if(std::uint64_t(tileSide+2*halo)*(tileSide+2*halo)*(40*n+160)>tileAllowance)
        throw std::runtime_error("Too many panorama captures for the available compositing memory. Use a smaller selection.");
    const unsigned total=((layout.width+tileSide-1)/tileSide)*((layout.height+tileSide-1)/tileSide);unsigned done=0;
    for(unsigned y=0;y<layout.height;y+=tileSide)for(unsigned x=0;x<layout.width;x+=tileSide) {
        Progress(request,ProcessingStage::PanoramaComposite,.64+.34*done/total,done++,total);
        // Align the expanded tile to pyramid levels so adjacent tiles produce
        // identical filter phases and have the full Gaussian support.
        const cv::Rect core(int(x),int(y),std::min(tileSide,int(layout.width-x)),std::min(tileSide,int(layout.height-y)));
        const cv::Rect region= cv::Rect(core.x-halo,core.y-halo,core.width+2*halo,core.height+2*halo)&cv::Rect(0,0,int(layout.width),int(layout.height));
        std::vector<Warped> images;std::vector<cv::Mat> masks;
        for(unsigned i=0;i<n;++i){images.push_back(ReadWarped(request,data.captures[i],layout.cameras[i],layout,region));masks.emplace_back(cv::Mat::zeros(region.size(),CV_32F));}
        cv::Mat selected(region.size(),CV_32S,cv::Scalar(-1)),fallback=cv::Mat::zeros(region.size(),CV_8U),fused(region.size(),CV_32FC3,cv::Scalar(0));
        cv::Mat tileVariance=cv::Mat::zeros(region.size(),CV_32F),tileSupport=cv::Mat::zeros(region.size(),CV_32F);
        for(int yy=0;yy<region.height;++yy){CheckCanceled(request);for(int xx=0;xx<region.width;++xx) {
            const int sx=std::clamp(int((xx+region.x+.5)*seams.scale),0,seams.owners.cols-1),sy=std::clamp(int((yy+region.y+.5)*seams.scale),0,seams.owners.rows-1);
            int owner=seams.owners.at<int>(sy,sx);
            if(owner<0||images[owner].mask.at<float>(yy,xx)==0){owner=-1;for(unsigned i=0;i<n;++i)if(images[i].mask.at<float>(yy,xx)>0){owner=int(i);break;}}
            if(owner<0)continue;
            selected.at<int>(yy,xx)=owner;masks[owner].at<float>(yy,xx)=1;
            const auto base=images[owner].rgb.at<cv::Vec3f>(yy,xx);const float baseVar=images[owner].variance.at<float>(yy,xx);
            const float baseWeight=1/std::max(1e-9f,baseVar);cv::Vec3d sum=cv::Vec3d(base)*baseWeight;double weight=baseWeight,squares=double(baseWeight)*baseWeight;
            bool conflict=false;unsigned validCount=1;
            for(unsigned i=0;i<n;++i)if(int(i)!=owner&&images[i].mask.at<float>(yy,xx)>0) {
                ++validCount;
                const auto other=images[i].rgb.at<cv::Vec3f>(yy,xx);const float otherVar=images[i].variance.at<float>(yy,xx);
                float difference=0;for(int c=0;c<3;++c)difference=std::max(difference,std::abs(other[c]-base[c]));
                const float sigma=std::sqrt(std::max(1e-10f,baseVar+otherVar));
                const float tolerance=3*sigma+.002f*std::max(.1f,std::max({base[0],base[1],base[2]}));
                if(difference>tolerance){conflict=true;continue;}
                if(!request.recipe.panorama.overlapDenoise||!data.captures[i].trustedNoise||!data.captures[owner].trustedNoise||
                   images[i].clipping.at<float>(yy,xx)>.01f||images[owner].clipping.at<float>(yy,xx)>.01f)continue;
                // Do not average unresolved native edges just because their
                // pixel colors happen to agree at one location.
                float gradient=0,patchError=0;
                for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox) {
                    const int px=std::clamp(xx+ox,0,region.width-1),py=std::clamp(yy+oy,0,region.height-1);
                    if(images[i].mask.at<float>(py,px)==0||images[owner].mask.at<float>(py,px)==0){patchError=1e9f;continue;}
                    const auto a=images[owner].rgb.at<cv::Vec3f>(py,px),b=images[i].rgb.at<cv::Vec3f>(py,px);
                    for(int c=0;c<3;++c){gradient=std::max(gradient,std::abs(a[c]-base[c]));patchError=std::max(patchError,std::abs(a[c]-b[c]));}
                }
                if(gradient>std::max(.01f,4*sigma)||patchError>tolerance)continue;
                const double w=1/std::max(1e-9f,otherVar);sum+=cv::Vec3d(other)*w;weight+=w;squares+=w*w;
            }
            fused.at<cv::Vec3f>(yy,xx)=cv::Vec3f(sum/weight);tileVariance.at<float>(yy,xx)=float(1/weight);
            tileSupport.at<float>(yy,xx)=float(weight*weight/squares);fallback.at<unsigned char>(yy,xx)=conflict||validCount==1?255:0;
        }}
        for(int yy=0;yy<region.height;++yy)for(int xx=0;xx<region.width;++xx) {
            const int owner=selected.at<int>(yy,xx);if(owner>=0)images[owner].rgb.at<cv::Vec3f>(yy,xx)=fused.at<cv::Vec3f>(yy,xx);
        }
        // Expand conflicting regions so the pyramid cannot smear a moving
        // contour into nearby stationary pixels.
        cv::dilate(fallback,fallback,cv::getStructuringElement(cv::MORPH_ELLIPSE,{7,7}));
        const auto blended=MultiBand(images,masks);
        for(int yy=core.y;yy<core.y+core.height;++yy)for(int xx=core.x;xx<core.x+core.width;++xx) {
            const int tx=xx-region.x,ty=yy-region.y,owner=selected.at<int>(ty,tx);if(owner<0)continue;
            const auto p=std::size_t(yy)*layout.width+xx;
            const auto value=fallback.at<unsigned char>(ty,tx)?fused.at<cv::Vec3f>(ty,tx):blended.at<cv::Vec3f>(ty,tx);
            for(unsigned c=0;c<3;++c){if(!std::isfinite(value[c]))throw std::runtime_error("Panorama compositing produced a non-finite pixel.");raw->linearFloatBuffer[p*3+c]=value[c];}
            (*coverage)[p]=1;(*owners)[p]=std::uint16_t(owner);(*variance)[p]=tileVariance.at<float>(ty,tx);(*support)[p]=tileSupport.at<float>(ty,tx);
            (*clipping)[p]=images[owner].clipping.at<float>(ty,tx)>.01f;
        }
    }
}
}
