#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing::Sr {
// Circular moments retain the 2x2 Bayer period without hard phase bins.
// Horizontal, vertical and both diagonal directions expose collinear/repeated
// sampling that a capture count or separate x/y histograms cannot detect.
// A 2x output also needs diversity within a sensor pixel: four integer Bayer
// shifts can complete native colour without providing half-pixel measurements.
class SamplingCoverage {
public:
    explicit SamplingCoverage(unsigned outputScale=1):harmonics_(outputScale>1?2u:1u) {}
    void Add(double dx,double dy,double weight) {
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(weight)||weight<=0)return;
        constexpr double pi=3.14159265358979323846;
        const double x=pi*std::remainder(dx,2.),y=pi*std::remainder(dy,2.);
        const std::array<double,4> phase={x,y,x+y,x-y};
        for(unsigned h=0;h<harmonics_;++h)for(unsigned i=0;i<phase.size();++i) {
            const auto index=h*phase.size()+i;
            real_[index]+=weight*std::cos((h+1)*phase[i]);
            imaginary_[index]+=weight*std::sin((h+1)*phase[i]);
        }
        weight_+=weight;squared_+=weight*weight;
    }
    float Confidence()const {
        const double total=weight_*weight_,independent=total-squared_;
        if(!(independent>1e-12*total)||!(squared_>0))return 0;
        double coverage=1;
        for(unsigned i=0;i<4*harmonics_;++i)
            coverage=std::min(coverage,std::clamp((total-real_[i]*real_[i]-
                imaginary_[i]*imaginary_[i])/independent,0.,1.));
        return float(coverage*std::clamp((total/squared_-1)*.5,0.,1.));
    }
private:
    unsigned harmonics_=1;
    std::array<double,8> real_{},imaginary_{};
    double weight_=0,squared_=0;
};
}
