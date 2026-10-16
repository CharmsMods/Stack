#include "Depth.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing::Depth {
bool RenderDefocus(const std::vector<float>& rgb,unsigned width,unsigned height,
    const ReconstructionDepth& depth,const DepthSettings& settings,double originX,double originY,
    double stepX,double stepY,std::vector<float>& output,const std::function<bool()>& cancel,std::string& error) {
    if(rgb.size()!=std::size_t(width)*height*3||!width||!height||stepX<=0||stepY<=0) {
        error="Invalid depth-of-field raster.";return false;
    }
    output=rgb;
    if(!settings.enabled||settings.blurRadius<=0||!depth.Available())return true;
    constexpr unsigned tileSide=192,layers=16;
    const double radiusScale=settings.blurRadius/std::max(stepX,stepY);
    const auto radius=[&](double d) {return radiusScale*std::clamp(
        (std::abs(d-settings.focus)-settings.focusRange*.5)/std::max(.05,std::max(settings.focus,1-settings.focus)),0.,1.);};
    const int halo=int(std::ceil(radiusScale*1.5))+2;
    // Bounded tiles retain the whole Gaussian footprint. Blend premultiplied
    // camera-linear layers from far to near, preserving HDR values and sign.
    for(unsigned top=0;top<height;top+=tileSide)for(unsigned left=0;left<width;left+=tileSide) {
        if(cancel&&cancel()){error="Canceled";return false;}
        const int x0=std::max(0,int(left)-halo),y0=std::max(0,int(top)-halo);
        const int x1=std::min(int(width),int(left+tileSide)+halo),y1=std::min(int(height),int(top+tileSide)+halo);
        const int w=x1-x0,h=y1-y0;
        std::vector<DepthSample> samples(std::size_t(w)*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            samples[std::size_t(y)*w+x]=SampleReconstructionDepth(depth,originX+(x0+x)*stepX,originY+(y0+y)*stepY);
        for(unsigned layer=0;layer<layers;++layer) {
            if(cancel&&cancel()){error="Canceled";return false;}
            const double center=double(layer)/(layers-1),sigma=radius(center)*.5;
            cv::Mat plane(h,w,CV_32FC4,cv::Scalar(0,0,0,0));bool populated=false;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const auto d=samples[std::size_t(y)*w+x];if(d.confidence<.3)continue;
                const float alpha=float(std::max(0.,1-std::abs(d.inverseDepth-center)*(layers-1)));
                if(alpha<=0)continue;populated=true;
                const auto p=(std::size_t(y0+y)*width+x0+x)*3;
                plane.at<cv::Vec4f>(y,x)={rgb[p]*alpha,rgb[p+1]*alpha,rgb[p+2]*alpha,alpha};
            }
            if(!populated)continue;
            if(sigma>.25) {
                const int kernel=2*int(std::ceil(3*sigma))+1;
                cv::GaussianBlur(plane,plane,cv::Size(kernel,kernel),sigma,sigma,cv::BORDER_REPLICATE);
            }
            for(unsigned y=top;y<std::min(height,top+tileSide);++y)for(unsigned x=left;x<std::min(width,left+tileSide);++x) {
                const auto d=samples[std::size_t(y-y0)*w+x-x0];if(d.confidence<.3)continue;
                // A farther layer cannot paint over an observed nearer surface.
                // Foreground blur can extend over background, as an aperture does.
                if(d.inverseDepth>center+1.01/(layers-1))continue;
                const auto value=plane.at<cv::Vec4f>(int(y)-y0,int(x)-x0);
                const float alpha=std::clamp(value[3],0.f,1.f);
                if(alpha<1e-6)continue;
                const auto p=(std::size_t(y)*width+x)*3;
                for(unsigned c=0;c<3;++c)output[p+c]=value[c]+(1-alpha)*output[p+c];
            }
        }
    }
    return true;
}
}
