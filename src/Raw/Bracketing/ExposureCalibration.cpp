#include "ProcessingInternal.h"
#include "PresentationEvidence.h"
#include "Alignment.h"
#include "ExposureGraph.h"
#include "ExposureEvidence.h"
#include "BurstNoise.h"
#include "RadiometricConsistency.h"
#include <array>

namespace Raw::Bracketing {
namespace {
double ProxyNoiseVariance(const PreparedSource& source,double signal,unsigned channel) {
    double variance=0;
    for(unsigned site=0;site<4;++site) {
        if((site==0?0:site==3?2:1)!=channel)continue;
        const auto& p=source.noise.sites[site];
        const double weight=channel==1?.5:1.;
        variance+=weight*weight*(p.shotScale*std::max(0.,signal)+p.offsetVariance+
            (p.quantizationIncluded?0:p.quantizationVariance));
    }
    return variance;
}
double QuantizedSignalFloor(const PreparedSource& source) {
    double floor=1e-5;
    for(double span:source.frame.calibration.usableSpanByCfaSite)
        if(std::isfinite(span)&&span>0) floor=std::max(floor,2./span);
    return floor;
}
bool PatchRatio(const PreparedSource& a,const PreparedSource& b,const ExposurePatch& patchA,
    const ExposurePatch& patchB,double signalFloorA,double signalFloorB,
    double& ratio,double& ratioVariance) {
    std::array<double,3> totalA{},totalB{};unsigned count=0;
    const auto valid=patchA.valid&patchB.valid;
    for(unsigned sample=0;sample<25;++sample) if(valid&(1u<<sample)) {
        for(unsigned c=0;c<3;++c) {totalA[c]+=patchA.color[sample][c];totalB[c]+=patchB.color[sample][c];}
        ++count;
    }
    if(count<16) return false;
    std::array<double,3> ev{},variance{};std::array<bool,3> usable{};
    // Average linear measurements before taking a ratio. Thresholding each
    // dark noisy sample and then taking logs biases the exposure estimate.
    // A modest correlation allowance covers bilinear proxy sampling.
    // Repeated one-code values are not independent sub-code measurements.
    for(unsigned c=0;c<3;++c) {
        const double x=totalA[c]/count,y=totalB[c]/count;
        const double va=2*ProxyNoiseVariance(a,x,c)/count,vb=2*ProxyNoiseVariance(b,y,c)/count;
        if(x<=std::max(signalFloorA,3*std::sqrt(va))||
            y<=std::max(signalFloorB,3*std::sqrt(vb)))continue;
        ev[c]=std::log2(x/y);
        variance[c]=std::max(1e-12,(va/(x*x)+vb/(y*y))/(std::log(2.)*std::log(2.)));
        usable[c]=true;
    }
    return ConsistentExposureRatio(ev,variance,usable,ratio,ratioVariance);
}
bool MatchingDenoiseScale(const PreparedSource& a,const PreparedSource& b,double& ev) {
    const auto& ma=a.metadata;const auto& mb=b.metadata;
    if(a.group!=b.group||!ma.hasExposureTime||!mb.hasExposureTime||!ma.hasIsoSpeed||!mb.hasIsoSpeed||
       !(ma.exposureTimeSeconds>0)||!(mb.exposureTimeSeconds>0)||!(ma.isoSpeed>0)||!(mb.isoSpeed>0)) return false;
    const double apertureA=ma.apertureFNumber>0?ma.apertureFNumber:1.0;
    const double apertureB=mb.apertureFNumber>0?mb.apertureFNumber:1.0;
    const double shutter=std::log2(ma.exposureTimeSeconds/mb.exposureTimeSeconds)-2*std::log2(apertureA/apertureB);
    const double iso=std::log2(ma.isoSpeed/mb.isoSpeed);
    ev=shutter+iso;return std::isfinite(ev)&&std::abs(shutter)<=.15+1e-6&&std::abs(iso)<=.05+1e-6;
}
}
bool Calibrate(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    const auto n=data.sources.size();
    if(n==0||data.origin>=n) {error="Exposure calibration has no reference capture.";return false;}
    std::vector<bool> fixed(n,false);
    std::vector<double> gains(n,0),variance;
    fixed[data.origin]=true;
    for(std::size_t i=0;i<n;++i) {
        gains[i]=std::log2(data.sources[i].scale);
        if(i==data.origin) {gains[i]=0;continue;}
        for(const auto& group:request.recipe.groups) for(const auto& frame:group.frames)
            if(frame.id==data.sources[i].id&&frame.manualExposure) {fixed[i]=true;gains[i]=-frame.relativeEv;}
    }
    std::vector<ExposureEdge> edges;
    std::vector<std::string> overlapEvidence;
    // Spatial bins prevent a dense textured object from dominating all of the
    // exposure evidence. Cost is bounded per pair even for very large sensors.
    ExposureEvidenceCache evidence(request,data);
    std::vector<double> signalFloors;signalFloors.reserve(n);
    for(const auto& source:data.sources)signalFloors.push_back(QuantizedSignalFloor(source));
    for(std::size_t a=0;a<n;++a) for(std::size_t b=a+1;b<n;++b) {
        if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
        std::array<std::vector<double>,64> tiles,tileVariance;
        const auto& samplesA=evidence.Get(a);const auto& samplesB=evidence.Get(b);
        for(std::size_t p=0;p<evidence.points.size();++p) {
            double ratio=0,uncertainty=0;
            if(!PatchRatio(data.sources[a],data.sources[b],samplesA[p],samplesB[p],
                signalFloors[a],signalFloors[b],ratio,uncertainty)) continue;
            tiles[evidence.points[p].spatialBin].push_back(ratio);
            tileVariance[evidence.points[p].spatialBin].push_back(uncertainty);
        }
        std::vector<double> tileRatios;
        tileRatios.reserve(tiles.size());
        double measurementVariance=0;
        for(unsigned bin=0;bin<tiles.size();++bin)if(tiles[bin].size()>=4) {
            tileRatios.push_back(MedianNoiseValue(std::move(tiles[bin])));
            // Overlapping patches within a bin are correlated. Only the
            // spatial bins reduce this uncertainty, not every proxy tap.
            measurementVariance+=MedianNoiseValue(std::move(tileVariance[bin]));
        }
        bool measured=false;
        if(tileRatios.size()>=4) {
            std::sort(tileRatios.begin(),tileRatios.end());
            const double median=MedianNoiseValue(tileRatios);
            const double spread=tileRatios[tileRatios.size()*3/4]-tileRatios[tileRatios.size()/4];
            const double sigma=std::max({.0001,1.58*spread/std::sqrt(static_cast<double>(tileRatios.size())),
                std::sqrt(measurementVariance)/tileRatios.size()});
            if(spread<.5&&sigma<.04) {edges.push_back({a,b,median,sigma*sigma});measured=true;}
            overlapEvidence.push_back(data.sources[a].id+" / "+data.sources[b].id+": bins="+
                std::to_string(tileRatios.size())+", EV="+std::to_string(median)+", spread="+
                std::to_string(spread)+", sigma="+std::to_string(sigma));
        }
        double metadataEv=0;
        if(!measured&&MatchingDenoiseScale(data.sources[a],data.sources[b],metadataEv))
            edges.push_back({a,b,metadataEv,.03*.03});
    }
    if(!SolveExposureGraph(edges,fixed,gains,variance)) {
        diagnostics.insert(diagnostics.end(),overlapEvidence.begin(),overlapEvidence.end());
        auto connected=fixed;
        for(std::size_t pass=0;pass<n;++pass) for(const auto& edge:edges)
            if(connected[edge.a]||connected[edge.b]) connected[edge.a]=connected[edge.b]=true;
        std::string missing;
        for(std::size_t i=0;i<n;++i) if(!connected[i]) {
            const auto input=std::find_if(request.sources.begin(),request.sources.end(),
                [&](const auto& source){return source.frameId==data.sources[i].id;});
            const auto name=input!=request.sources.end()&&!input->displayName.empty()?input->displayName:
                std::filesystem::path(data.sources[i].metadata.sourcePath).filename().string();
            if(!missing.empty()) missing+=", ";missing+=name.empty()?data.sources[i].id:name;
        }
        error="Exposure overlap could not connect "+(missing.empty()?std::string("every capture"):missing)+
            " to the fixed origin. Enter relative EV for those inputs in the capture controls.";
        return false;
    }
    for(std::size_t i=0;i<n;++i) {
        auto& source=data.sources[i];source.scale=std::exp2(gains[i]);
        source.scaleVariance=std::pow(std::log(2.)*source.scale,2)*variance[i];
    }
    for(std::size_t i=0;i<n;++i) {
        const auto& source=data.sources[i];
        ReportPresentation(request,ProcessingStage::Exposure,i+1,n,source.id,"Relative exposure "+std::to_string(-gains[i])+" EV");
        ObserveExposure(request,data,source);
        diagnostics.push_back(source.id+": joint exposure "+std::to_string(-gains[i])+" EV, estimated uncertainty "+
            std::to_string(std::sqrt(variance[i]))+" EV.");
    }
    return true;
}
}
