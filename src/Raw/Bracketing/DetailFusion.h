#pragma once
#include "ProcessingInternal.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
struct CoarseMeasurement {
    double value=0,variance=0,centerCovariance=0;
};
// A two-band temporal estimate. Soft captures keep their contribution to the
// low-frequency signal while their detail preference changes only the detail
// band. The covariance term is necessary: the two bands share measurements.
struct DetailFusionAccumulator {
    double baseWeight=0,detailWeight=0,baseSquared=0,detailSquared=0;
    double base=0,detail=0,baseVariance=0,detailVariance=0,crossVariance=0;
    double baseUncertainty=0,detailUncertainty=0;
    void Add(const Observation& sample,const CoarseMeasurement& coarse,double preference) {
        if(sample.support<=0||!sample.finite||sample.clipped) return;
        const double w=1/std::max(1e-12,sample.variance);
        const double d=w*std::clamp(preference,.05,1.);
        const double covariance=std::clamp(coarse.centerCovariance,0.,
            std::sqrt(std::max(0.,coarse.variance*sample.measurementVariance)));
        baseWeight+=w;detailWeight+=d;baseSquared+=w*w;detailSquared+=d*d;
        base+=w*coarse.value;detail+=d*(sample.value-coarse.value);
        baseVariance+=w*w*coarse.variance;
        detailVariance+=d*d*std::max(0.,sample.measurementVariance+coarse.variance-2*covariance);
        crossVariance+=w*d*(covariance-coarse.variance);
        baseUncertainty+=w*w*sample.uncertaintyVariance;
        detailUncertainty+=d*d*sample.uncertaintyVariance;
    }
    void Finish(Observation& result) const {
        if(baseWeight<=0||detailWeight<=0) return;
        result.value=base/baseWeight+detail/detailWeight;
        result.measurementVariance=std::max(1e-12,baseVariance/(baseWeight*baseWeight)+
            detailVariance/(detailWeight*detailWeight)+2*crossVariance/(baseWeight*detailWeight));
        // Registration/calibration errors can correlate across a neighborhood.
        // Keep a conservative band bound separate from measurement noise.
        result.uncertaintyVariance=baseUncertainty/(baseWeight*baseWeight)+detailUncertainty/(detailWeight*detailWeight);
        result.variance=result.measurementVariance+result.uncertaintyVariance;
        result.support=std::min(baseWeight*baseWeight/baseSquared,detailWeight*detailWeight/detailSquared);
    }
};

inline CoarseMeasurement CoarseSample(const std::vector<Observation>& samples,unsigned width,
    unsigned height,unsigned x,unsigned y,double correlationArea) {
    const auto& center=samples[y*width+x];
    CoarseMeasurement result;double total=0,centerWeight=0;
    constexpr double kernel[3]={1,2,1};
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        const int xx=static_cast<int>(x)+2*dx,yy=static_cast<int>(y)+2*dy;
        if(xx<0||yy<0||xx>=static_cast<int>(width)||yy>=static_cast<int>(height)) continue;
        const auto& s=samples[static_cast<std::size_t>(yy)*width+xx];
        if(!s.finite||s.clipped||s.support<=0) continue;
        const double w=kernel[dx+1]*kernel[dy+1];
        result.value+=w*s.value;result.variance+=w*w*s.measurementVariance;total+=w;
        if(!dx&&!dy) centerWeight=w;
    }
    if(total<=0) return {center.value,center.measurementVariance,center.measurementVariance};
    result.value/=total;result.variance*=correlationArea/(total*total);
    result.centerCovariance=std::min(center.measurementVariance,
        correlationArea*centerWeight/total*center.measurementVariance);
    return result;
}
}
