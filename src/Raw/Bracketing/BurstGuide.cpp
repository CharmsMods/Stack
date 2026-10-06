#include "BurstGuide.h"
#include "BurstSample.h"
#include "LocalDetailNoise.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
bool RepeatedBurstStructure(const std::array<double,25>& a,const std::array<double,25>& b,
    const std::array<double,25>& va,const std::array<double,25>& vb,
    Mfd::RawSignalGradient& gradient,double& disagreement) {
    double ax=0,ay=0,bx=0,by=0,an=0,bn=0;
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
        const unsigned i=(y+2)*5+x+2;const double xx=2*x,yy=2*y;
        ax+=xx*a[i]/200;ay+=yy*a[i]/200;bx+=xx*b[i]/200;by+=yy*b[i]/200;
        an+=(xx*xx+yy*yy)*va[i]/40000;bn+=(xx*xx+yy*yy)*vb[i]/40000;
    }
    const double cross=ax*bx+ay*by;
    disagreement=((ax-bx)*(ax-bx)+(ay-by)*(ay-by))/std::max(1e-12,an+bn);
    const double uncertainty=std::sqrt(std::max(0.,an*(bx*bx+by*by)+bn*(ax*ax+ay*ay)+an*bn));
    gradient={(ax+bx)*.5,(ay+by)*.5};
    return cross>3*uncertainty&&disagreement<9;
}

bool MeasureBurstGuide(const ProcessingRequest& request,const PreparedDataset& data,
    PreparedTileSampler& sampler,unsigned x,unsigned y,BurstGuidePoint& guide,std::string& error) {
    guide={};
    if(x<4||y<4||x+5>=data.width||y+5>=data.height)return true;
    std::array<std::array<std::array<double,25>,4>,2> sum{},precision{},noise{};
    std::array<unsigned,2> members{};
    struct CaptureNoise {
        unsigned subset=0;
        std::array<std::array<double,25>,4> weights{};
        std::array<double,4> measurement{},model{};
    };
    std::vector<CaptureNoise> captureNoise;
    const auto& origin=data.sources[data.origin];unsigned ordinal=0;
    std::array<double,4> pilot{};std::array<unsigned,4> pilotCount{};
    for(unsigned c=0;c<4;++c)for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
        Mfd::SameCfaTapInput tap;
        if(!sampler.ReadSample(origin.frame,x+c%2+2*dx,y+c/2+2*dy,tap,&error))return false;
        if(tap.sampleFlags==0&&std::isfinite(tap.normalizedSample)){pilot[c]+=tap.normalizedSample*tap.comparisonGain;++pilotCount[c];}
    }
    for(unsigned c=0;c<4;++c){if(!pilotCount[c])return true;pilot[c]/=pilotCount[c];}
    for(const auto& source:data.sources) {
        if(!source.enabled)continue;
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        const unsigned subset=ordinal++%2;++members[subset];
        CaptureNoise measured;measured.subset=subset;
        std::array<LocalDetailNoise,4> covariance;
        for(unsigned c=0;c<4;++c)for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
            const auto s=ReadBurstSample(data,source,sampler,x+c%2+2*dx,y+c/2+2*dy,pilot[c],error);
            if(!error.empty())return false;
            if(!s.finite||s.clipped||s.support<=0||s.headroom<.5)continue;
            if(!covariance[c].Add(data,source,sampler,x+c%2+2*dx,y+c/2+2*dy,pilot[c],s,error)) {
                if(!error.empty())return false;
                continue;
            }
            const unsigned i=(dy+2)*5+dx+2;const double w=1/std::max(1e-12,s.variance);
            measured.weights[c][i]=w;
            sum[subset][c][i]+=w*s.value;precision[subset][c][i]+=w;
            noise[subset][c][i]+=w*w*s.variance;
        }
        for(unsigned c=0;c<4;++c) {
            measured.measurement[c]=covariance[c].MeasurementBound();
            measured.model[c]=covariance[c].UncertaintyBound();
        }
        captureNoise.push_back(std::move(measured));
    }
    if(ordinal<3||!members[0]||!members[1])return true;
    bool repeated=false;
    for(unsigned c=0;c<4;++c) {
        std::array<double,2> sensorBound{},modelSd{};
        for(const auto& capture:captureNoise) {
            double weight=0;
            for(unsigned i=0;i<25;++i) {
                if(precision[capture.subset][c][i]<=0)return true;
                weight=std::max(weight,capture.weights[c][i]/precision[capture.subset][c][i]);
            }
            sensorBound[capture.subset]+=weight*weight*capture.measurement[c];
            modelSd[capture.subset]+=weight*std::sqrt(capture.model[c]);
        }
        for(unsigned i=0;i<25;++i)for(unsigned subset=0;subset<2;++subset) {
            const double w=precision[subset][c][i];if(w<=0)return true;
            sum[subset][c][i]/=w;noise[subset][c][i]/=w*w;
        }
        // A variance bound for every orthonormal direction also bounds these
        // fitted slopes. Interpolated neighbors are not independent samples.
        for(unsigned subset=0;subset<2;++subset)
            noise[subset][c].fill(sensorBound[subset]+modelSd[subset]*modelSd[subset]);
        double disagreement=0;
        repeated|=RepeatedBurstStructure(sum[0][c],sum[1][c],noise[0][c],noise[1][c],guide.gradient[c],disagreement);
        if(disagreement>16)return true;
        guide.mean[c]=(sum[0][c][12]+sum[1][c][12])*.5;
        guide.variance[c]=(noise[0][c][12]+noise[1][c][12])*.25;
    }
    guide.repeated=repeated;return true;
}
}
