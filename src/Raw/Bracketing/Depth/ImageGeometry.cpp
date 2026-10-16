#include "Geometry.h"
#include <opencv2/calib3d.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing::Depth {
namespace {
double Median(std::vector<double> values) {
    const auto mid=values.begin()+values.size()/2;std::nth_element(values.begin(),mid,values.end());return *mid;
}
cv::Matx33d Camera(double focal,const PairGeometry& pair) {
    return {focal,0,pair.cx,0,focal,pair.cy,0,0,1};
}
// An essential matrix has two equal nonzero singular values. Fit that
// image-derived constraint with square pixels and a centered principal point.
// No camera make, lens identifier, focal-length tag or device table is used.
double EssentialError(const cv::Matx33d& fundamental,const PairGeometry& pair,double logFocal,double logRatio) {
    const double focal=std::exp(logFocal),other=focal*std::exp(logRatio);
    const auto essential=Camera(other,pair).t()*fundamental*Camera(focal,pair);
    cv::Vec3d singular;cv::SVD::compute(essential,singular);
    return (singular[0]-singular[1])/std::max(1e-20,singular[0]+singular[1]);
}
struct Hypothesis {
    PairGeometry pose;
    std::vector<double> inverse;
    double error=0;
};
// Bougnoux's two-view self-calibration equations, evaluated in centered
// coordinates. Singular or imaginary solutions are discarded; they are
// common for near-parallel camera axes and do not establish absent depth.
bool FocalSeed(const cv::Matx33d& fundamental,const PairGeometry& pair,double& reference,double& source) {
    const cv::Matx33d center(1,0,pair.cx,0,1,pair.cy,0,0,1);
    const auto f=center.t()*fundamental*center;
    cv::Mat singular,u,vt;cv::SVD::compute(cv::Mat(f),singular,u,vt);
    const auto squared=[](const cv::Matx33d& matrix,double ex,double ey) {
        const double numerator=-matrix(2,2)*(ex*matrix(1,2)-ey*matrix(0,2));
        const double denominator=ex*(matrix(1,0)*matrix(2,0)+matrix(1,1)*matrix(2,1))-
            ey*(matrix(0,0)*matrix(2,0)+matrix(0,1)*matrix(2,1));
        return denominator!=0?numerator/denominator:std::numeric_limits<double>::quiet_NaN();
    };
    reference=std::sqrt(squared(f,u.at<double>(0,2),u.at<double>(1,2)));
    source=std::sqrt(squared(f.t(),vt.at<double>(2,0),vt.at<double>(2,1)));
    return std::isfinite(reference)&&std::isfinite(source)&&reference>0&&source>0;
}
bool Recover(const cv::Matx33d& fundamental,const std::vector<cv::Point2d>& a,const std::vector<cv::Point2d>& b,
    const cv::Mat& consensus,Hypothesis& candidate) {
    auto& pose=candidate.pose;
    std::vector<cv::Point2d> left,right;left.reserve(a.size());right.reserve(b.size());
    for(std::size_t i=0;i<a.size();++i) {
        left.emplace_back((a[i].x-pose.cx)/pose.focal,(a[i].y-pose.cy)/pose.focal);
        right.emplace_back((b[i].x-pose.cx)/pose.sourceFocal,(b[i].y-pose.cy)/pose.sourceFocal);
    }
    const auto essential=Camera(pose.sourceFocal,pose).t()*fundamental*Camera(pose.focal,pose);
    cv::Mat rotation,translation,mask=consensus.clone();
    const auto total=cv::countNonZero(consensus);
    const auto count=cv::recoverPose(essential,left,right,cv::Matx33d::eye(),rotation,translation,100000.,mask);
    if(count<30||count<total*.7)return false;
    for(int y=0;y<3;++y) {
        pose.translation[y]=translation.at<double>(y);
        for(int x=0;x<3;++x)pose.rotation(y,x)=rotation.at<double>(y,x);
    }
    std::vector<double> shifts,errors;candidate.inverse.assign(a.size(),0);
    for(std::size_t i=0;i<a.size();++i)if(mask.at<uchar>(int(i))) {
        double inverse=0,shift=0,error=0;
        if(Triangulate(pose,{a[i].x,a[i].y},{b[i].x,b[i].y},inverse,shift,error)) {
            candidate.inverse[i]=inverse;shifts.push_back(shift);errors.push_back(error);
        }
    }
    if(shifts.size()<30||shifts.size()<total*.6)return false;
    pose.inliers=unsigned(shifts.size());pose.parallax=Median(shifts);candidate.error=Median(errors);
    return pose.parallax>=1.25&&candidate.error<.75;
}
}

bool SolveGeometry(const std::vector<cv::Point2d>& a,const std::vector<cv::Point2d>& b,
    PairGeometry& pair,std::string& reason) {
    if(a.size()<40||a.size()!=b.size()) {
        reason="Too few reliable scene correspondences ("+std::to_string(a.size())+").";return false;
    }
    cv::Mat planarMask;
    const auto plane=cv::findHomography(a,b,cv::RANSAC,.75,planarMask,1000,.999);
    const auto planar=plane.empty()?0:cv::countNonZero(planarMask);
    if(planar>=a.size()*.85) {
        reason="Motion is ambiguous between a plane and camera rotation ("+
            std::to_string(planar)+" of "+std::to_string(a.size())+" matches).";return false;
    }
    cv::Mat mask;
    const auto fitted=cv::findFundamentalMat(a,b,cv::FM_RANSAC,1.,.999,2000,mask);
    if(fitted.rows!=3||fitted.cols!=3||cv::countNonZero(mask)<a.size()*.65) {
        reason="Image correspondences do not support one camera motion.";return false;
    }
    const cv::Matx33d fundamental=fitted;
    const double diagonal=2*std::hypot(pair.cx+.5,pair.cy+.5);
    if(!(diagonal>0)){reason="Invalid image extent for camera geometry.";return false;}
    // Search a broad, dimensionless range. Keep multiple solutions instead of
    // assuming the lens calibration itself is observable in a short burst.
    // A change in image scale between views can account for focus breathing.
    const double lo=std::log(diagonal*.15),hi=std::log(diagonal*20),ratioLimit=std::log(1.5);
    std::vector<Hypothesis> candidates;
    const auto recover=[&](double focal,double sourceFocal) {
        if(focal<=std::exp(lo)||focal>=std::exp(hi)||std::abs(std::log(sourceFocal/focal))>=ratioLimit)return;
        Hypothesis candidate;candidate.pose=pair;candidate.pose.focal=focal;candidate.pose.sourceFocal=sourceFocal;
        if(Recover(fundamental,a,b,mask,candidate))candidates.push_back(std::move(candidate));
    };
    double referenceFocal=0,sourceFocal=0;
    if(FocalSeed(fundamental,pair,referenceFocal,sourceFocal))recover(referenceFocal,sourceFocal);
    for(unsigned seed=0;seed<21;++seed) {
      for(bool optimizeFocal:{false,true}) {
        double focal=lo+(hi-lo)*seed/20,ratio=0;
        double best=EssentialError(fundamental,pair,focal,ratio);
        double focalStep=(hi-lo)/40,ratioStep=ratioLimit/4;
        for(unsigned iteration=0;iteration<28;++iteration) {
            double nextFocal=focal,nextRatio=ratio;
            for(int f=-1;f<=1;++f)for(int r=-1;r<=1;++r) {
                if(f!=0&&!optimizeFocal)continue;
                const double testFocal=std::clamp(focal+f*focalStep,lo,hi);
                const double testRatio=std::clamp(ratio+r*ratioStep,-ratioLimit,ratioLimit);
                const double error=EssentialError(fundamental,pair,testFocal,testRatio);
                if(error<best) {best=error;nextFocal=testFocal;nextRatio=testRatio;}
            }
            if(nextFocal==focal&&nextRatio==ratio) {focalStep*=.5;ratioStep*=.5;}
            focal=nextFocal;ratio=nextRatio;
        }
        if(best>.02||focal<=lo+.001||focal>=hi-.001||std::abs(ratio)>=ratioLimit-.001)continue;
        recover(std::exp(focal),std::exp(focal+ratio));
      }
    }
    if(candidates.empty()) {
        reason="Image-derived camera geometry is unstable or parallax is below sampling precision.";return false;
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& left,const auto& right){return left.error<right.error;});
    const auto& best=candidates.front();unsigned plausible=0;double worstMedian=0;
    for(const auto& candidate:candidates) {
        if(candidate.error>best.error+.15||candidate.pose.inliers<best.pose.inliers*.8)continue;
        std::vector<double> ratios;
        for(std::size_t i=0;i<a.size();++i)if(best.inverse[i]>0&&candidate.inverse[i]>0)
            ratios.push_back(best.inverse[i]/candidate.inverse[i]);
        if(ratios.size()<best.pose.inliers*.75) {
            reason="Plausible camera solutions disagree on visible scene structure.";return false;
        }
        const double scale=Median(ratios);std::vector<double> differences;unsigned agreeing=0;
        for(double value:ratios) {const double difference=std::abs(std::log(value/scale));
            differences.push_back(difference);agreeing+=difference<.2;}
        const double difference=Median(differences);worstMedian=std::max(worstMedian,difference);
        if(agreeing<ratios.size()*.85||difference>.08) {
            reason="Relative depth changes across equally plausible camera solutions.";return false;
        }
        ++plausible;
    }
    pair=best.pose;pair.calibrationConfidence=std::clamp(1-worstMedian*3,.6,1.);
    if(plausible<2)pair.calibrationConfidence*=.8;
    return true;
}
}
