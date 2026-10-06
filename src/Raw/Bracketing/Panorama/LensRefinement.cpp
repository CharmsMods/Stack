#include "Internal.h"
#include <opencv2/calib3d.hpp>
#include <cmath>

namespace Raw::Bracketing::Panorama {
namespace {
struct Match {unsigned a,b;cv::Point2d p,q;double scale;};
class LensFit final:public cv::LMSolver::Callback {
public:
    const ProcessingRequest& request;
    const Prepared& data;
    std::vector<Match> points;
    explicit LensFit(const ProcessingRequest& r,const Prepared& d):request(r),data(d) {
        const auto n=d.captures.size();
        for(unsigned a=0;a<n;++a)for(unsigned b=a+1;b<n;++b) {
            const auto& m=d.matches[a*n+b];if(m.confidence<1||m.H.empty())continue;
            const auto stride=std::max<std::size_t>(1,m.matches.size()/128);
            for(std::size_t k=0;k<m.matches.size();k+=stride)if(m.inliers_mask[k]) {
                const auto& pair=m.matches[k];auto p=d.features[a].keypoints[pair.queryIdx].pt,q=d.features[b].keypoints[pair.trainIdx].pt;
                points.push_back({a,b,cv::Point2d(p)/d.captures[a].proxyScale,cv::Point2d(q)/d.captures[b].proxyScale,d.captures[b].proxyScale});
            }
        }
    }
    std::vector<Camera> Decode(const cv::Mat& parameters)const {
        auto cameras=data.layout.cameras;const auto n=cameras.size();
        for(unsigned i=0;i<n;++i) {
            auto& c=cameras[i];c.focal*=std::exp(parameters.at<double>(int(i)*4+3));
            if(i!=data.reference) {
                cv::Mat rv=(cv::Mat_<double>(3,1)<<parameters.at<double>(i*4),parameters.at<double>(i*4+1),parameters.at<double>(i*4+2));
                cv::Mat delta;cv::Rodrigues(rv,delta);cv::Mat base(3,3,CV_64F,c.rotation.data());cv::Mat rotated=base*delta;
                std::copy(rotated.ptr<double>(),rotated.ptr<double>()+9,c.rotation.begin());
            }
            c.k1=parameters.at<double>(int(n)*4);c.k2=parameters.at<double>(int(n)*4+1);
        }
        return cameras;
    }
    cv::Mat Errors(const cv::Mat& p)const {
        CheckCanceled(request);const auto cameras=Decode(p);const auto n=cameras.size();
        cv::Mat errors(int(points.size()*2+n+2),1,CV_64F);int k=0;
        for(const auto& m:points) {
            const auto& a=cameras[m.a];const auto& b=cameras[m.b];
            double u=(m.p.x-a.cx)/a.focal,v=(m.p.y-a.cy)/a.focal;const double xd=u,yd=v;
            for(int i=0;i<6;++i){double r=u*u+v*v,d=std::max(.25,1+a.k1*r+a.k2*r*r);u=xd/d;v=yd/d;}
            const auto ray=cv::Matx33d(b.rotation.data()).t()*cv::Matx33d(a.rotation.data())*cv::Vec3d(u,v,1);
            const double x=ray[0]/std::max(1e-4,ray[2]),y=ray[1]/std::max(1e-4,ray[2]),r=x*x+y*y,d=1+b.k1*r+b.k2*r*r;
            double dx=(b.focal*x*d+b.cx-m.q.x)*m.scale,dy=(b.focal*y*d+b.cy-m.q.y)*m.scale;
            const double weight=std::sqrt(std::min(1.,3/std::max(1e-9,std::hypot(dx,dy))));
            errors.at<double>(k++)=dx*weight;errors.at<double>(k++)=dy*weight;
        }
        for(unsigned i=0;i<n;++i)errors.at<double>(k++)=p.at<double>(i*4+3)/.15;
        errors.at<double>(k++)=p.at<double>(int(n)*4)/.08;
        errors.at<double>(k++)=p.at<double>(int(n)*4+1)/.04;
        return errors;
    }
    bool compute(cv::InputArray input,cv::OutputArray error,cv::OutputArray jacobian)const override {
        const auto p=input.getMat();auto e=Errors(p);e.copyTo(error);
        if(jacobian.needed()) {
            cv::Mat j(e.rows,p.rows,CV_64F,cv::Scalar(0));
            for(int c=0;c<p.rows;++c) {
                if(c/4==int(data.reference)&&c%4<3)continue;
                cv::Mat shifted=p.clone();shifted.at<double>(c)+=1e-5;auto d=(Errors(shifted)-e)/1e-5;
                cv::Mat column=d;column.copyTo(j.col(c));
            }
            j.copyTo(jacobian);
        }
        return true;
    }
};
}
void RefineLens(const ProcessingRequest& request,Prepared& data) {
    if(data.captures.size()<3)return;
    auto fit=cv::makePtr<LensFit>(request,data);if(fit->points.size()<100)return;
    cv::Mat p=cv::Mat::zeros(int(data.captures.size()*4+2),1,CV_64F);
    const double initial=cv::norm(fit->Errors(p),cv::NORM_L2SQR);
    auto solver=cv::LMSolver::create(fit,30,1e-5);if(solver->run(p)<0||!cv::checkRange(p))return;
    const auto candidate=fit->Decode(p);
    if(cv::norm(fit->Errors(p),cv::NORM_L2SQR)>initial*.97)return;
    for(const auto& c:candidate) {
        if(std::abs(c.k1)>.25||std::abs(c.k2)>.10||c.focal<.2*std::min(c.width,c.height))return;
        const double corner=std::hypot(std::max(c.cx,c.width-c.cx),std::max(c.cy,c.height-c.cy))/c.focal;
        for(int s=0;s<=32;++s){double r=std::pow(corner*s/32,2);if(1+3*c.k1*r+5*c.k2*r*r<.4)return;}
    }
    data.layout.cameras=candidate;
}
}
