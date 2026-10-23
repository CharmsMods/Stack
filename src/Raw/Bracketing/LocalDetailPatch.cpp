#include "LocalDetailPatch.h"
#include "LocalDetail.h"
#include "LocalDetailNoise.h"
#include "BurstSample.h"
#include "DetailFusion.h"
#include "PreparedRegion.h"
#include "SceneGradient.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
namespace {
struct CapturePatch {
    std::array<std::vector<Observation>,4> samples;
    LocalDetailCapture evidence;
    std::array<double,4> meanBase{},baseVariation{},bandDifference{},baseNorm{};
};
bool Pilot(const PreparedSource& origin,PreparedTileSampler& sampler,unsigned x,unsigned y,double& value,std::string& error) {
    value=0;unsigned count=0;
    for(unsigned dy=0;dy<4;dy+=2)for(unsigned dx=0;dx<4;dx+=2) {
        Mfd::SameCfaTapInput tap;
        if(!sampler.ReadSample(origin.frame,(x/4)*4+x%2+dx,(y/4)*4+y%2+dy,tap,&error))return false;
        if(tap.sampleFlags!=0||!std::isfinite(tap.normalizedSample*tap.comparisonGain))continue;
        value+=tap.normalizedSample*tap.comparisonGain;++count;
    }
    value=count?std::max(0.,value/count):0;return count>0;
}
}
struct LocalDetailPatchCache::State {
    static constexpr unsigned extent=80;
    struct Guide {
        unsigned x=std::numeric_limits<unsigned>::max(),y=std::numeric_limits<unsigned>::max();
        Mfd::RawSignalGradient gradient{};
        double pilot=0;
        bool gradientReady=false,pilotReady=false;
    };
    struct Sample {
        unsigned x=std::numeric_limits<unsigned>::max(),y=std::numeric_limits<unsigned>::max();
        Observation observation;
        LocalDetailNoise::Row noise;
        bool ready=false;
    };
    const ProcessingRequest* request;
    const PreparedDataset* data;
    std::vector<std::size_t> sources;
    std::vector<Guide> guides;
    std::vector<std::vector<Sample>> samples;
    std::size_t sampleRows=0;
    bool initialized=false;

    State(const ProcessingRequest& input,const PreparedDataset& prepared,const std::vector<std::size_t>& indices)
        :request(&input),data(&prepared),sources(indices) {}
    void Initialize() {
        if(initialized)return;
        initialized=true;
        const auto budget=LocalDetailPatchCacheBytes(request->memoryBudgetBytes);
        const auto guideBytes=sizeof(Guide)*extent*extent;
        const auto fixedBytes=sizeof(State)+sources.size()*(sizeof(std::size_t)+sizeof(std::vector<Sample>))+guideBytes;
        if(budget<fixedBytes)return;
        guides.resize(extent*extent);
        if(sources.empty())return;
        sampleRows=std::min<std::size_t>(extent,(budget-fixedBytes)/sizeof(Sample)/sources.size()/extent);
        if(sampleRows)samples.resize(sources.size());
    }
    Guide& GuideAt(unsigned x,unsigned y) {
        auto& guide=guides[std::size_t(y%extent)*extent+x%extent];
        if(guide.x!=x||guide.y!=y){guide={};guide.x=x;guide.y=y;}
        return guide;
    }
    Sample* SampleAt(std::size_t source,unsigned x,unsigned y) {
        if(!sampleRows)return nullptr;
        auto& capture=samples[source];
        if(capture.empty())capture.resize(extent*sampleRows);
        auto& sample=capture[(y%sampleRows)*extent+x%extent];
        if(sample.x!=x||sample.y!=y){sample.ready=false;sample.x=x;sample.y=y;}
        return &sample;
    }
    bool PrepareGuides(PreparedTileSampler& sampler,unsigned left,unsigned top,unsigned rawSide,
        std::vector<Mfd::RawSignalGradient>& gradients,std::array<std::vector<double>,4>& pilots,
        bool& valid,std::string& error) {
        valid=true;
        if(request->recipe.alignmentMode==AlignmentMode::AutomaticLocal) {
            unsigned missingLeft=rawSide,missingTop=rawSide,missingRight=0,missingBottom=0;
            for(unsigned y=0;y<rawSide;++y)for(unsigned x=0;x<rawSide;++x) {
                if(GuideAt(left+x,top+y).gradientReady)continue;
                missingLeft=std::min(missingLeft,x);missingTop=std::min(missingTop,y);
                missingRight=std::max(missingRight,x+1);missingBottom=std::max(missingBottom,y+1);
            }
            if(missingRight>missingLeft) {
                // Compute only the missing strip/bounds through the original
                // gradient owner. Its halo and per-pixel operation order are
                // independent of the requested region's size and origin.
                Mfd::PreparedRawTile region;
                if(!ReadPreparedRegion(data->sources[data->origin].frame,sampler,
                    left+missingLeft,top+missingTop,missingRight-missingLeft,missingBottom-missingTop,region,error))return false;
                std::vector<Mfd::RawSignalGradient> missing;
                if(!BuildSceneGradients(*request,*data,region,missing,error,sampler))return false;
                const auto stride=missingRight-missingLeft;
                for(unsigned y=missingTop;y<missingBottom;++y)for(unsigned x=missingLeft;x<missingRight;++x) {
                    auto& guide=GuideAt(left+x,top+y);
                    guide.gradient=missing[std::size_t(y-missingTop)*stride+x-missingLeft];
                    guide.gradientReady=true;
                }
            }
            for(unsigned y=0;y<rawSide;++y)for(unsigned x=0;x<rawSide;++x)
                gradients[std::size_t(y)*rawSide+x]=GuideAt(left+x,top+y).gradient;
        }
        const auto side=rawSide/2;
        for(unsigned c=0;c<4;++c) {
            pilots[c].resize(side*side);
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
                const auto rx=left+2*x+c%2,ry=top+2*y+c/2;
                auto& guide=GuideAt(rx,ry);
                if(!guide.pilotReady) {
                    if(!Pilot(data->sources[data->origin],sampler,rx,ry,guide.pilot,error)) {
                        valid=false;return error.empty();
                    }
                    guide.pilotReady=true;
                }
                pilots[c][y*side+x]=guide.pilot;
            }
        }
        return true;
    }
};

LocalDetailPatchCache::LocalDetailPatchCache(const ProcessingRequest& request,const PreparedDataset& data,
    const std::vector<std::size_t>& sources):m_State(std::make_unique<State>(request,data,sources)) {}
LocalDetailPatchCache::~LocalDetailPatchCache()=default;

bool EvaluateLocalDetailPatch(const ProcessingRequest& request,const PreparedDataset& data,
    const std::vector<std::size_t>& sources,PreparedTileSampler& sampler,unsigned centerX,unsigned centerY,
    const LocalDetailTransform& transform,LocalDetailPatchResult& out,std::string& error,LocalDetailPatchCache* cache) {
    out={};const unsigned n=transform.Size(),side=n+2,rawSide=2*side;
    if(sources.size()<3||centerX<n+4||centerY<n+4||centerX+n+4>=data.width||centerY+n+4>=data.height)return true;
    // This immutable geometry gate used to run after each capture's sampling,
    // noise bounds and transforms. A failing capture discards the whole patch;
    // establish that same admission before doing any of that pixel work.
    std::vector<double> alignmentReliability(sources.size(),1.);
    for(std::size_t f=0;f<sources.size();++f) {
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        const auto& source=data.sources[sources[f]];
        if(source.localMotion) {
            Mfd::LocalMotionFieldSample motion;Mfd::LocalMotionOptions options;
            if(!EvaluateCaptureMotion(source,{double(centerX),double(centerY)},options,motion)||motion.alignmentConfidence<.4){out.centerRejected=true;return true;}
            alignmentReliability[f]=motion.alignmentConfidence;
        }
    }
    const unsigned left=centerX-n-2,top=centerY-n-2;
    auto* reuse=cache?cache->m_State.get():nullptr;
    if(reuse&&(reuse->request!=&request||reuse->data!=&data||reuse->sources!=sources))reuse=nullptr;
    // Most dark/noisy patches can fail the center gate above. They must not
    // allocate or initialize the reuse buffers merely to reject immediately.
    if(reuse){reuse->Initialize();if(reuse->guides.empty())reuse=nullptr;}
    std::vector<Mfd::RawSignalGradient> gradients(rawSide*rawSide,{std::numeric_limits<double>::quiet_NaN(),0});
    std::array<std::vector<double>,4> pilots;
    if(reuse) {
        bool valid=false;
        if(!reuse->PrepareGuides(sampler,left,top,rawSide,gradients,pilots,valid,error))return false;
        if(!valid)return true;
    } else {
        Mfd::PreparedRawTile origin;
        if(!ReadPreparedRegion(data.sources[data.origin].frame,sampler,left,top,rawSide,rawSide,origin,error))return false;
        if(request.recipe.alignmentMode==AlignmentMode::AutomaticLocal&&
            !BuildSceneGradients(request,data,origin,gradients,error,sampler))return false;
        for(unsigned c=0;c<4;++c) {
            pilots[c].resize(side*side);
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x)
                if(!Pilot(data.sources[data.origin],sampler,left+2*x+c%2,top+2*y+c/2,pilots[c][y*side+x],error))return error.empty();
        }
    }
    std::vector<CapturePatch> captures(sources.size());bool bands=false;
    for(std::size_t f=0;f<sources.size();++f) {
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        const auto& source=data.sources[sources[f]];auto& capture=captures[f];bands|=source.detailPreference<.999;
        for(unsigned c=0;c<4;++c) {
            auto& samples=capture.samples[c];samples.resize(side*side);LocalDetailNoise noise;
            std::vector<double> signal(n*n);double precision=0;
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
                const unsigned rx=left+2*x+c%2,ry=top+2*y+c/2,p=y*side+x;
                auto& sample=samples[p];
                auto* cached=reuse?reuse->SampleAt(f,rx,ry):nullptr;
                if(cached&&cached->ready)sample=cached->observation;
                else sample=ReadBurstSample(data,source,sampler,rx,ry,pilots[c][p],error,
                        &gradients[(2*y+c/2)*rawSide+2*x+c%2]);
                if(!error.empty())return false;
                // A partial patch cannot overrule a local rejection or a clip.
                // It keeps the established per-pixel temporal fallback.
                if(!sample.finite||sample.clipped||sample.support<=0||sample.localRejected||!std::isfinite(sample.headroom)||sample.headroom<.99){out.sampleRejected=true;return true;}
                if(cached&&cached->ready)noise.Add(sample,cached->noise);
                else if(cached) {
                    if(!LocalDetailNoise::PrepareRow(data,source,sampler,rx,ry,pilots[c][p],sample,cached->noise,error))return error.empty();
                    noise.Add(sample,cached->noise);
                    cached->observation=sample;cached->ready=true;
                } else if(!noise.Add(data,source,sampler,rx,ry,pilots[c][p],sample,error))return error.empty();
                if(x&&y&&x<=n&&y<=n) {
                    signal[(y-1)*n+x-1]=sample.value;precision+=1/sample.variance/(n*n);
                }
            }
            capture.evidence.coefficients[c]=transform.Forward(signal);
            capture.evidence.measurementBound[c]=noise.MeasurementBound();
            capture.evidence.uncertaintyBound[c]=noise.UncertaintyBound();
            capture.evidence.precision[c]=precision;
        }
        capture.evidence.alignmentReliability=alignmentReliability[f];
    }
    std::vector<LocalDetailCapture> evidence;for(auto& capture:captures)evidence.push_back(std::move(capture.evidence));
    const auto selected=AnalyzeLocalDetail(evidence,n,request.shouldCancel);
    if(selected.canceled){error="Canceled";return false;}
    out.mayExpand=selected.mayExpand;
    if(!selected.selectedCount)return true;
    out.captureSupport=selected.minimumCaptureSupport;
    for(unsigned c=0;c<4;++c) {
        std::vector<double> baseline(n*n),detailWeights(sources.size()*n*n);
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x) {
            const unsigned p=y*n+x,q=(y+1)*side+x+1;double sum=0,detailSum=0,value=0;
            DetailFusionAccumulator accumulator;
            for(std::size_t f=0;f<captures.size();++f) {
                const auto& sample=captures[f].samples[c][q];
                const double h=std::isfinite(sample.headroom)?std::clamp(sample.headroom,0.,1.):0.;
                const double w=h/sample.variance;
                sum+=w;detailSum+=w*std::clamp(data.sources[sources[f]].detailPreference,.05,1.);value+=w*sample.value;
                if(bands) {
                    // Full valid same-CFA 3x3 support. Signal math is identical
                    // to the established temporal detail merger.
                    double coarse=0;constexpr double kernel[3]={1,2,1};
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                        coarse+=kernel[dx+1]*kernel[dy+1]*captures[f].samples[c][std::size_t(int(y)+1+dy)*side+int(x)+1+dx].value/16;
                    accumulator.Add(sample,{coarse,0,0},data.sources[sources[f]].detailPreference,h);
                }
            }
            baseline[p]=value/sum;
            if(bands) {Observation merged;accumulator.Finish(merged);baseline[p]=merged.value;}
            for(std::size_t f=0;f<captures.size();++f) {
                const auto& sample=captures[f].samples[c][q];
                const double h=std::isfinite(sample.headroom)?std::clamp(sample.headroom,0.,1.):0.;
                const double w=h/sample.variance;
                const double a=w/sum;
                const double d=bands?w*std::clamp(data.sources[sources[f]].detailPreference,.05,1.)/detailSum:a;
                detailWeights[f*n*n+p]=d;captures[f].meanBase[c]+=a/(n*n);
                captures[f].bandDifference[c]=std::max(captures[f].bandDifference[c],std::abs(a-d));
                captures[f].baseNorm[c]=std::max(captures[f].baseNorm[c],std::abs(d));
            }
        }
        const auto baseCoefficients=transform.Forward(baseline);std::vector<double> correction(n*n);
        for(unsigned k=1;k<n*n;++k)if(selected.selected[k]) {
            for(std::size_t f=0;f<captures.size();++f)correction[k]+=selected.weights[f][k]*evidence[f].coefficients[c][k];
            correction[k]-=baseCoefficients[k];
        }
        auto spatial=transform.Inverse(correction);out.correction[c].resize(16*16);
        const unsigned offset=(n-16)/2;double mean=0,mass=0;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
            const double w=LocalDetailWindow(x,y);mean+=w*spatial[(y+offset)*n+x+offset];mass+=w;
        }
        mean/=mass;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
            out.correction[c][y*16+x]=spatial[(y+offset)*n+x+offset]-mean;
        double uncertaintySd=0,baselineUncertaintySd=0;
        for(std::size_t f=0;f<captures.size();++f) {
            auto& capture=captures[f];double selectionChange=0;
            for(unsigned k=0;k<n*n;++k) {
                capture.baseVariation[c]=std::max(capture.baseVariation[c],std::abs(detailWeights[f*n*n+k]-capture.meanBase[c]));
                if(selected.selected[k])selectionChange=std::max(selectionChange,std::abs(selected.weights[f][k]-capture.meanBase[c]));
            }
            // The binomial coarse operator has norm <= 1. Bound the change
            // from the old, spatially varying temporal weights to each new
            // frequency weight, rather than pretending DCT noise is diagonal.
            const double norm=selectionChange+capture.baseVariation[c]+capture.bandDifference[c];
            const double baseNorm=capture.baseNorm[c]+capture.bandDifference[c];
            out.measurementBound[c]+=norm*norm*evidence[f].measurementBound[c];
            out.baselineMeasurementBound[c]+=baseNorm*baseNorm*evidence[f].measurementBound[c];
            uncertaintySd+=norm*std::sqrt(evidence[f].uncertaintyBound[c]);
            baselineUncertaintySd+=baseNorm*std::sqrt(evidence[f].uncertaintyBound[c]);
        }
        out.uncertaintyBound[c]=uncertaintySd*uncertaintySd;
        out.baselineUncertaintyBound[c]=baselineUncertaintySd*baselineUncertaintySd;
    }
    out.accepted=true;return true;
}
}
