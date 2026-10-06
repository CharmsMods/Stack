#include "LocalDetailEvidence.h"
#include "LocalDetailTransform.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Raw::Bracketing {
namespace {
double Noise(const LocalDetailCapture& capture,unsigned c) {
    return std::max(1e-12,capture.measurementBound[c]+capture.uncertaintyBound[c]);
}
// Fit a common, positive local response using cross-capture products. The
// diagonal is noise corrected. Held-out coefficients must support the same
// response before it can affect a third coefficient set.
LocalDetailDecision Fit(const std::vector<LocalDetailCapture>& captures,
    const std::vector<unsigned>& indices,std::vector<double>& response,const std::function<bool()>& canceled) {
    const auto f=captures.size();if(indices.size()<4)return LocalDetailDecision::Weak;
    std::vector<double> gram(f*f);double floorSquared=0;
    for(unsigned c=0;c<4;++c) {
        double scale=0;for(const auto& capture:captures)scale+=Noise(capture,c)/f;
        for(std::size_t i=0;i<f;++i) {
            if(canceled&&canceled())return LocalDetailDecision::Canceled;
            const double vi=Noise(captures[i],c)/scale;
            for(std::size_t j=0;j<f;++j) {
                for(const auto k:indices)gram[i*f+j]+=captures[i].coefficients[c][k]*captures[j].coefficients[c][k]/scale;
                floorSquared+=indices.size()*vi*Noise(captures[j],c)/scale;
            }
            gram[i*f+i]-=vi*indices.size();floorSquared+=indices.size()*vi*vi;
        }
    }
    const double floor=std::sqrt(floorSquared);
    response.assign(f,1/std::sqrt(double(f)));
    for(unsigned iteration=0;iteration<16;++iteration) {
        if(canceled&&canceled())return LocalDetailDecision::Canceled;
        std::vector<double> next(f);double norm=0;
        for(std::size_t i=0;i<f;++i) {
            for(std::size_t j=0;j<f;++j)next[i]+=gram[i*f+j]*response[j];
            norm+=next[i]*next[i];
        }
        if(norm<=1e-20)return LocalDetailDecision::Weak;
        for(std::size_t i=0;i<f;++i)response[i]=next[i]/std::sqrt(norm);
    }
    if(std::accumulate(response.begin(),response.end(),0.)<0)for(auto& r:response)r=-r;
    double energy=0,residual=0;
    for(std::size_t i=0;i<f;++i)for(std::size_t j=0;j<f;++j)energy+=response[i]*gram[i*f+j]*response[j];
    if(energy<6*floor)return LocalDetailDecision::Weak;
    if(*std::min_element(response.begin(),response.end())<=0)return LocalDetailDecision::Contradictory;
    for(std::size_t i=0;i<f;++i)for(std::size_t j=0;j<f;++j) {
        const double r=gram[i*f+j]-energy*response[i]*response[j];residual+=r*r;
    }
    if(std::sqrt(residual)>std::max(.18*energy,4*floor))return LocalDetailDecision::Contradictory;
    const double maximum=*std::max_element(response.begin(),response.end());
    for(auto& r:response)r/=maximum;
    return LocalDetailDecision::Consistent;
}
bool ValidateColors(const std::vector<LocalDetailCapture>& captures,const std::vector<unsigned>& indices,
    const std::vector<double>& response) {
    double responseSquared=0;for(const auto r:response)responseSquared+=r*r;
    for(unsigned c=0;c<4;++c) {
        double residual=0,energy=0,noise=0;
        for(const auto k:indices) {
            double scene=0;for(std::size_t f=0;f<captures.size();++f)scene+=response[f]*captures[f].coefficients[c][k];
            scene/=responseSquared;
            for(std::size_t f=0;f<captures.size();++f) {
                const double r=captures[f].coefficients[c][k]-response[f]*scene;
                residual+=r*r;energy+=response[f]*response[f]*scene*scene;noise+=Noise(captures[f],c);
            }
        }
        if(residual>3*noise+.08*energy)return false;
    }
    return true;
}
}
LocalDetailEvidence AnalyzeLocalDetail(const std::vector<LocalDetailCapture>& captures,unsigned size,
    const std::function<bool()>& canceled) {
    LocalDetailEvidence out;const unsigned count=size*size;const auto f=captures.size();
    if(canceled&&canceled()){out.canceled=true;return out;}
    if(f<3||(size!=16&&size!=32))return out;
    for(const auto& capture:captures)for(const auto& c:capture.coefficients)if(c.size()!=count)return out;
    out.selected.assign(count,0);out.weights.assign(f,std::vector<double>(count));
    out.minimumCaptureSupport=double(f);bool contradictory=false,weak=false;
    for(unsigned band=0;band<9;++band) {
        std::array<std::vector<unsigned>,3> folds;unsigned ordinal=0;
        for(unsigned k=1;k<count;++k)if(LocalDetailBand(k%size,k/size,size)==band)folds[ordinal++%3].push_back(k);
        for(unsigned fold=0;fold<3;++fold) {
            const auto& fit=folds[(fold+1)%3];const auto& validate=folds[(fold+2)%3];
            std::vector<double> response,held;
            const auto decision=Fit(captures,fit,response,canceled);
            if(decision==LocalDetailDecision::Canceled){out.canceled=true;return out;}
            if(decision==LocalDetailDecision::Weak){weak=true;continue;}
            if(decision==LocalDetailDecision::Contradictory){contradictory=true;continue;}
            const auto check=Fit(captures,validate,held,canceled);
            if(check==LocalDetailDecision::Canceled){out.canceled=true;return out;}
            if(check==LocalDetailDecision::Weak){weak=true;continue;}
            if(check==LocalDetailDecision::Contradictory||!ValidateColors(captures,validate,response)) {
                contradictory=true;continue;
            }
            double largestDifference=0;
            for(std::size_t i=0;i<f;++i)largestDifference=std::max(largestDifference,std::abs(response[i]-held[i]));
            const auto best=std::size_t(std::max_element(response.begin(),response.end())-response.begin());
            const auto worst=std::size_t(std::min_element(response.begin(),response.end())-response.begin());
            if(largestDifference>.12||held[best]<held[worst]+.08)continue;
            // The evidence and validation sets must both find a material
            // response difference. Near ties keep all temporal measurements.
            if(response[worst]>.85)continue;
            std::vector<double> weights(f);double sum=0,squared=0;
            for(std::size_t i=0;i<f;++i) {
                double precision=0;for(unsigned c=0;c<4;++c)precision+=captures[i].precision[c]*.25;
                const double r=std::min(response[i],held[i]);
                weights[i]=precision*std::pow(std::clamp(r,.1,1.),4)*std::clamp(captures[i].alignmentReliability,0.,1.);
                sum+=weights[i];
            }
            if(sum<=0)continue;
            for(auto& w:weights){w/=sum;squared+=w*w;}
            out.minimumCaptureSupport=std::min(out.minimumCaptureSupport,1/squared);
            for(const auto k:folds[fold]) {
                out.selected[k]=1;++out.selectedCount;
                for(std::size_t i=0;i<f;++i)out.weights[i][k]=weights[i];
            }
        }
    }
    // A contradictory patch cannot borrow a larger neighborhood to overrule
    // a local motion or color boundary.
    if(contradictory){out.selectedCount=0;std::fill(out.selected.begin(),out.selected.end(),0);}
    bool repeatable=false;
    for(unsigned c=0;c<4;++c) {
        // Two disjoint capture subsets must carry some repeatable structure
        // before spending work on the larger neighborhood. Pure grain keeps
        // the baseline without a second, larger search for an accidental fit.
        double cross=0,uncertainty=0;
        for(unsigned k=1;k<count;++k) {
            double a=0,b=0,va=0,vb=0;unsigned na=0,nb=0;
            for(std::size_t i=0;i<f;++i) {
                if(i%2){b+=captures[i].coefficients[c][k];vb+=Noise(captures[i],c);++nb;}
                else {a+=captures[i].coefficients[c][k];va+=Noise(captures[i],c);++na;}
            }
            a/=na;b/=nb;va/=na*na;vb/=nb*nb;
            cross+=a*b;uncertainty+=va*b*b+vb*a*a+va*vb;
        }
        repeatable|=cross>3*std::sqrt(uncertainty);
    }
    out.mayExpand=out.selectedCount==0&&weak&&!contradictory&&repeatable;
    return out;
}
}
