#pragma once

#include <array>
#include <cmath>

namespace Raw::Bracketing {

// Local coordinates are relative to the output point. This is only the
// weighted-plane algebra: callers own sample validity, edge/model checks and
// variance propagation. Positive observation weights do not imply positive
// reconstruction coefficients.
template<class Real> struct LocalAffineFit {
    Real weight=0, meanX=0, meanY=0, meanValue=0;
    Real inverseXX=0, inverseXY=0, inverseYY=0;
    Real value=0, slopeX=0, slopeY=0;

    // Value, x derivative and y derivative coefficients for one observation.
    std::array<Real,3> Coefficients(Real x,Real y,Real observationWeight) const {
        const Real dx=x-meanX,dy=y-meanY,normalized=observationWeight/weight;
        const Real gx=normalized*(inverseXX*dx+inverseXY*dy);
        const Real gy=normalized*(inverseXY*dx+inverseYY*dy);
        return {normalized-meanX*gx-meanY*gy,gx,gy};
    }
};

template<class Real> struct LocalAffineMoments {
    Real weight=0,x=0,y=0,xx=0,xy=0,yy=0,value=0,xValue=0,yValue=0;

    void Add(Real dx,Real dy,Real observation,Real observationWeight) {
        weight+=observationWeight;
        x+=observationWeight*dx;y+=observationWeight*dy;
        xx+=observationWeight*dx*dx;xy+=observationWeight*dx*dy;
        yy+=observationWeight*dy*dy;value+=observationWeight*observation;
        xValue+=observationWeight*dx*observation;
        yValue+=observationWeight*dy*observation;
    }

    bool Fit(LocalAffineFit<Real>& out,Real minimumDeterminantRatio=Real(1e-4)) const {
        out={};
        if(!std::isfinite(weight)||weight<=0||!std::isfinite(minimumDeterminantRatio)||
            minimumDeterminantRatio<=0)return false;
        out.weight=weight;out.meanX=x/weight;out.meanY=y/weight;out.meanValue=value/weight;
        const Real a=xx/weight-out.meanX*out.meanX;
        const Real b=xy/weight-out.meanX*out.meanY;
        const Real c=yy/weight-out.meanY*out.meanY;
        const Real trace=a+c,determinant=a*c-b*b;
        if(!std::isfinite(trace)||!std::isfinite(determinant)||a<=0||c<=0||
            determinant<=0||determinant<minimumDeterminantRatio*trace*trace)return false;
        out.inverseXX=c/determinant;out.inverseXY=-b/determinant;out.inverseYY=a/determinant;
        const Real vx=xValue/weight-out.meanX*out.meanValue;
        const Real vy=yValue/weight-out.meanY*out.meanValue;
        out.slopeX=out.inverseXX*vx+out.inverseXY*vy;
        out.slopeY=out.inverseXY*vx+out.inverseYY*vy;
        out.value=out.meanValue-out.slopeX*out.meanX-out.slopeY*out.meanY;
        return std::isfinite(out.value)&&std::isfinite(out.slopeX)&&std::isfinite(out.slopeY);
    }
};

} // namespace Raw::Bracketing
