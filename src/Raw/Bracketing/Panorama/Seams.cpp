#include "Internal.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <cmath>

namespace Raw::Bracketing::Panorama {
namespace {
cv::Mat WarpProxy(const Capture& capture,const Camera& camera,const Layout& layout,const cv::Size size,double scale,cv::Mat& mask) {
    cv::Mat mx(size,CV_32F),my(size,CV_32F);mask=cv::Mat(size,CV_8U,cv::Scalar(0));
    for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x) {
        cv::Point2d p;const bool valid=Unproject(camera,layout,(x+.5)/scale-.5,(y+.5)/scale-.5,p);
        mx.at<float>(y,x)=valid?float((p.x+.5)*capture.proxyScale-.5):-1;
        my.at<float>(y,x)=valid?float((p.y+.5)*capture.proxyScale-.5):-1;
        mask.at<unsigned char>(y,x)=valid?255:0;
    }
    cv::Mat warped;cv::remap(capture.proxy,warped,mx,my,cv::INTER_LINEAR,cv::BORDER_CONSTANT);
    return warped;
}
double Luma(const cv::Vec3f& p){return (p[0]+2*p[1]+p[2])*.25;}
}
Seams MatchBrightnessAndSeams(const ProcessingRequest& request,Prepared& data) {
    const auto n=data.captures.size();const auto& layout=data.layout;
    const double limit=std::min(1600000.,double(request.memoryBudgetBytes)/std::max<std::size_t>(1,n)/96);
    Seams seams;seams.scale=std::min(1.,std::sqrt(limit/(double(layout.width)*layout.height)));
    const cv::Size size(std::max(1,int(std::ceil(layout.width*seams.scale))),std::max(1,int(std::ceil(layout.height*seams.scale))));
    std::vector<cv::Mat> warped(n),masks(n);
    for(unsigned i=0;i<n;++i){Progress(request,ProcessingStage::PanoramaSeams,.52+.04*i/n,i,unsigned(n));warped[i]=WarpProxy(data.captures[i],layout.cameras[i],layout,size,seams.scale,masks[i]);}
    // Solve a connected network of log-luminance ratios with a fixed reference.
    cv::Mat normal=cv::Mat::zeros(int(n),int(n),CV_64F),rhs=cv::Mat::zeros(int(n),1,CV_64F);
    for(unsigned i=0;i<n;++i)for(unsigned j=i+1;j<n;++j) {
        CheckCanceled(request);std::vector<double> ratios;
        for(int y=2;y<size.height-2;y+=3)for(int x=2;x<size.width-2;x+=3)if(masks[i].at<unsigned char>(y,x)&&masks[j].at<unsigned char>(y,x)) {
            const auto a=warped[i].at<cv::Vec3f>(y,x),b=warped[j].at<cv::Vec3f>(y,x);
            const double la=Luma(a),lb=Luma(b);
            if(la<.005||lb<.005||std::max({a[0],a[1],a[2],b[0],b[1],b[2]})>.90)continue;
            const double ga=std::abs(Luma(warped[i].at<cv::Vec3f>(y,x+1))-Luma(warped[i].at<cv::Vec3f>(y,x-1)));
            const double gb=std::abs(Luma(warped[j].at<cv::Vec3f>(y,x+1))-Luma(warped[j].at<cv::Vec3f>(y,x-1)));
            if(std::max(ga/la,gb/lb)>.12)continue;
            ratios.push_back(std::log(lb/la));
        }
        if(ratios.size()<32)continue;
        auto median=ratios.begin()+ratios.size()/2;std::nth_element(ratios.begin(),median,ratios.end());const double r=*median;
        const double weight=std::min(1000.,double(ratios.size()));
        normal.at<double>(i,i)+=weight;normal.at<double>(j,j)+=weight;normal.at<double>(i,j)-=weight;normal.at<double>(j,i)-=weight;
        rhs.at<double>(i)+=r*weight;rhs.at<double>(j)-=r*weight;
    }
    for(unsigned i=0;i<n;++i)normal.at<double>(i,i)+=1e-4;
    normal.at<double>(data.reference,data.reference)+=1e6;
    cv::Mat gains;if(!cv::solve(normal,rhs,gains,cv::DECOMP_CHOLESKY))throw std::runtime_error("Could not match panorama brightness.");
    std::vector<cv::UMat> images(n),seamMasks(n);std::vector<cv::Point> corners(n,cv::Point(0,0));
    for(unsigned i=0;i<n;++i) {
        data.layout.cameras[i].gain=std::exp(std::clamp(gains.at<double>(i),-std::log(16.),std::log(16.)));
        warped[i]*=data.layout.cameras[i].gain;
        // Bounded display guides are only for seam cost. Final values remain linear.
        cv::Mat guide=warped[i].clone();
        for(int y=0;y<guide.rows;++y)for(int x=0;x<guide.cols;++x)for(int c=0;c<3;++c) {
            float& v=guide.at<cv::Vec3f>(y,x)[c];v=255*std::sqrt(std::max(0.f,v)/(1+std::max(0.f,v)));
        }
        guide.copyTo(images[i]);masks[i].copyTo(seamMasks[i]);
    }
    Progress(request,ProcessingStage::PanoramaSeams,.58);
    cv::detail::GraphCutSeamFinder finder(cv::detail::GraphCutSeamFinderBase::COST_COLOR_GRAD);
    finder.find(images,corners,seamMasks);CheckCanceled(request);
    seams.owners=cv::Mat(size,CV_32S,cv::Scalar(-1));
    for(unsigned i=0;i<n;++i) {
        const auto mask=seamMasks[i].getMat(cv::ACCESS_READ);
        for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x)if(mask.at<unsigned char>(y,x))seams.owners.at<int>(y,x)=int(i);
    }
    // Graph cuts may leave uncovered overview samples. Assign only real support.
    for(int y=0;y<size.height;++y)for(int x=0;x<size.width;++x)if(seams.owners.at<int>(y,x)<0)
        for(unsigned i=0;i<n;++i)if(masks[i].at<unsigned char>(y,x)){seams.owners.at<int>(y,x)=int(i);break;}
    return seams;
}
}
