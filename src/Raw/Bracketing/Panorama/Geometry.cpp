#include "Internal.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/stitching/detail/motion_estimators.hpp>
#include <queue>
#include <set>
#include <cmath>

namespace Raw::Bracketing::Panorama {
namespace {
cv::Matx33d Rotation(const Camera& c){return cv::Matx33d(c.rotation.data());}
cv::Vec3d Ray(const Camera& c,double x,double y) {
    const double xd=(x-c.cx)/c.focal,yd=(y-c.cy)/c.focal;
    double u=xd,v=yd;
    for(int i=0;i<8;++i){double r=u*u+v*v,d=1+c.k1*r+c.k2*r*r;if(d<.2)break;u=xd/d;v=yd/d;}
    return Rotation(c)*cv::Vec3d(u,v,1);
}
bool Spread(const cv::detail::ImageFeatures& f,const std::vector<cv::DMatch>& matches,
    const std::vector<unsigned char>& inliers,bool query) {
    std::vector<cv::Point2f> points;
    for(std::size_t k=0;k<matches.size();++k)if(inliers[k])points.push_back(f.keypoints[query?matches[k].queryIdx:matches[k].trainIdx].pt);
    if(points.size()<12)return false;
    std::vector<cv::Point2f> hull;cv::convexHull(points,hull);
    return cv::contourArea(hull)>f.img_size.area()*.005;
}
}
cv::Point2d Project(const Camera& c,const Layout& l,double x,double y) {
    const auto ray=Ray(c,x,y);
    if(l.projection==PanoramaProjection::Perspective) {
        if(ray[2]<=1e-5)return {NAN,NAN};
        return {l.scale*ray[0]/ray[2]-l.offsetX,l.scale*ray[1]/ray[2]-l.offsetY};
    }
    return {l.scale*std::atan2(ray[0],ray[2])-l.offsetX,l.scale*std::atan2(ray[1],std::hypot(ray[0],ray[2]))-l.offsetY};
}
bool Unproject(const Camera& c,const Layout& l,double x,double y,cv::Point2d& point) {
    const double u=(x+l.offsetX)/l.scale,v=(y+l.offsetY)/l.scale;
    cv::Vec3d ray=l.projection==PanoramaProjection::Perspective?cv::Vec3d(u,v,1):
        cv::Vec3d(std::sin(u)*std::cos(v),std::sin(v),std::cos(u)*std::cos(v));
    ray=Rotation(c).t()*ray;
    if(ray[2]<=1e-6)return false;
    const double nx=ray[0]/ray[2],ny=ray[1]/ray[2],r=nx*nx+ny*ny,d=1+c.k1*r+c.k2*r*r;
    point={c.focal*nx*d+c.cx,c.focal*ny*d+c.cy};
    return std::isfinite(point.x)&&std::isfinite(point.y)&&point.x>=0&&point.y>=0&&point.x<=c.width-1&&point.y<=c.height-1;
}
void SolveLayout(const ProcessingRequest& request,Prepared& data) {
    const auto n=data.captures.size();data.features.resize(n);data.matches.resize(n*n);
    const auto detector=cv::SIFT::create(9000,3,.025);
    for(std::size_t i=0;i<n;++i) {
        Progress(request,ProcessingStage::PanoramaMatching,.22+.08*i/n,unsigned(i),unsigned(n),data.captures[i].source.frameId);
        cv::detail::computeImageFeatures(detector,data.captures[i].guide,data.features[i]);data.features[i].img_idx=int(i);
    }
    unsigned pair=0;const unsigned pairs=unsigned(n*(n-1)/2);
    std::vector<std::vector<unsigned>> edges(n);
    cv::BFMatcher matcher(cv::NORM_L2);
    for(unsigned i=0;i<n;++i)for(unsigned j=i+1;j<n;++j) {
        Progress(request,ProcessingStage::PanoramaMatching,.30+.13*pair/pairs,pair++,pairs);
        const auto& a=data.features[i];const auto& b=data.features[j];
        auto& m=data.matches[i*n+j];m.src_img_idx=int(i);m.dst_img_idx=int(j);
        if(a.keypoints.size()<12||b.keypoints.size()<12)continue;
        std::vector<std::vector<cv::DMatch>> forward,backward;
        matcher.knnMatch(a.descriptors,b.descriptors,forward,2);matcher.knnMatch(b.descriptors,a.descriptors,backward,2);
        for(const auto& p:forward)if(p.size()==2&&p[0].distance<.72f*p[1].distance) {
            const auto& back=backward[p[0].trainIdx];
            if(back.size()==2&&back[0].trainIdx==p[0].queryIdx&&back[0].distance<.72f*back[1].distance)m.matches.push_back(p[0]);
        }
        if(m.matches.size()<12)continue;
        std::vector<cv::Point2f> p,q;
        for(const auto& match:m.matches) {
            p.push_back(a.keypoints[match.queryIdx].pt-cv::Point2f(a.img_size.width*.5f,a.img_size.height*.5f));
            q.push_back(b.keypoints[match.trainIdx].pt-cv::Point2f(b.img_size.width*.5f,b.img_size.height*.5f));
        }
        m.H=cv::findHomography(p,q,cv::RANSAC,3,m.inliers_mask,3000,.995);
        if(m.H.empty()||!cv::checkRange(m.H)||!Spread(a,m.matches,m.inliers_mask,true)||!Spread(b,m.matches,m.inliers_mask,false)){m.H.release();continue;}
        m.num_inliers=int(std::count(m.inliers_mask.begin(),m.inliers_mask.end(),1));
        m.confidence=m.num_inliers/(8+.3*m.matches.size());
        if(m.confidence<1.0||m.num_inliers<12){m.H.release();m.confidence=0;continue;}
        auto& reverse=data.matches[j*n+i];reverse=m;reverse.src_img_idx=int(j);reverse.dst_img_idx=int(i);reverse.H=m.H.inv();
        for(auto& match:reverse.matches)std::swap(match.queryIdx,match.trainIdx);
        edges[i].push_back(j);edges[j].push_back(i);
    }
    std::vector<bool> reached(n);std::queue<unsigned> pending;pending.push(data.reference);reached[data.reference]=true;
    while(!pending.empty()){auto i=pending.front();pending.pop();for(auto j:edges[i])if(!reached[j]){reached[j]=true;pending.push(j);}}
    std::string missing;for(unsigned i=0;i<n;++i)if(!reached[i]){if(!missing.empty())missing+=", ";missing+=data.captures[i].source.displayName;}
    if(!missing.empty())throw std::runtime_error("Not enough reliable overlap to connect: "+missing+". Add overlapping captures or disable those images.");
    Progress(request,ProcessingStage::PanoramaLayout,.44);
    cv::detail::HomographyBasedEstimator estimator;
    std::vector<cv::detail::CameraParams> cameras;
    if(!estimator(data.features,data.matches,cameras))throw std::runtime_error("Could not estimate panorama camera geometry.");
    for(auto& c:cameras)c.R.convertTo(c.R,CV_32F);
    cv::detail::BundleAdjusterRay adjuster;adjuster.setConfThresh(1.0);
    adjuster.setTermCriteria({cv::TermCriteria::COUNT|cv::TermCriteria::EPS,80,1e-6});
    if(!adjuster(data.features,data.matches,cameras))throw std::runtime_error("The panorama camera layout did not converge.");
    CheckCanceled(request);
    const cv::Mat anchor=cameras[data.reference].R.t();
    data.layout.cameras.clear();
    for(unsigned i=0;i<n;++i) {
        const auto& source=data.captures[i];const auto& fitted=cameras[i];
        Camera camera;camera.frameId=source.source.frameId;camera.name=source.source.displayName;
        camera.width=source.width;camera.height=source.height;
        camera.focal=fitted.focal/source.proxyScale;camera.cx=fitted.ppx/source.proxyScale;camera.cy=fitted.ppy/source.proxyScale;
        cv::Mat rotated=anchor*fitted.R;rotated.convertTo(rotated,CV_64F);
        std::copy(rotated.ptr<double>(),rotated.ptr<double>()+9,camera.rotation.begin());
        if(!std::isfinite(camera.focal)||camera.focal<std::min(source.width,source.height)*.2||camera.focal>std::max(source.width,source.height)*20)
            throw std::runtime_error("Unreliable focal-length estimate for "+camera.name+". Use a rectilinear lens and more overlapping detail.");
        data.layout.cameras.push_back(camera);
    }
    RefineLens(request,data);
    Progress(request,ProcessingStage::PanoramaLayout,.50);
}
void ChooseCanvas(const ProcessingRequest& request,Layout& layout) {
    constexpr double degrees=180/3.14159265358979323846;
    double minTheta=1e9,maxTheta=-1e9,minPhi=1e9,maxPhi=-1e9;
    std::vector<double> focals;
    for(const auto& c:layout.cameras) {
        focals.push_back(c.focal);
        for(int edge=0;edge<4;++edge)for(int s=0;s<=64;++s) {
            const double t=s/64.;const double x=edge<2?t*(c.width-1):(edge==2?0:c.width-1);
            const double y=edge<2?(edge==0?0:c.height-1):t*(c.height-1);
            const auto r=Ray(c,x,y);const double th=std::atan2(r[0],r[2]),ph=std::atan2(r[1],std::hypot(r[0],r[2]));
            minTheta=std::min(minTheta,th);maxTheta=std::max(maxTheta,th);minPhi=std::min(minPhi,ph);maxPhi=std::max(maxPhi,ph);
        }
    }
    if((maxTheta-minTheta)*degrees>330||(maxPhi-minPhi)*degrees>170)
        throw std::runtime_error("This panorama reaches the wraparound or pole limit. Use a smaller selection for this version.");
    layout.projection=request.recipe.panorama.projection;
    if(layout.projection==PanoramaProjection::Auto)layout.projection=
        (maxTheta-minTheta)*degrees<=100&&(maxPhi-minPhi)*degrees<=100?PanoramaProjection::Perspective:PanoramaProjection::Spherical;
    std::sort(focals.begin(),focals.end());layout.scale=focals[focals.size()/2]*request.recipe.panorama.outputScale;
    layout.offsetX=layout.offsetY=0;double left=1e30,top=1e30,right=-1e30,bottom=-1e30;
    for(const auto& c:layout.cameras)for(int edge=0;edge<4;++edge)for(int s=0;s<=256;++s) {
        const double t=s/256.;const auto p=Project(c,layout,edge<2?t*(c.width-1):(edge==2?0:c.width-1),
            edge<2?(edge==0?0:c.height-1):t*(c.height-1));
        if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Perspective cannot cover this layout. Choose Spherical projection.");
        left=std::min(left,p.x);top=std::min(top,p.y);right=std::max(right,p.x);bottom=std::max(bottom,p.y);
    }
    layout.offsetX=std::floor(left);layout.offsetY=std::floor(top);
    const double w=std::ceil(right)-layout.offsetX+1,h=std::ceil(bottom)-layout.offsetY+1;
    if(w<1||h<1||w>2147483000||h>2147483000||!std::isfinite(w*h))throw std::runtime_error("Panorama canvas exceeds addressable dimensions.");
    layout.width=unsigned(w);layout.height=unsigned(h);
}
}
