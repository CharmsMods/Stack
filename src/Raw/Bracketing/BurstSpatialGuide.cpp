#include "BurstSpatialGuide.h"
#include "CaptureCorrespondence.h"
#include "PreparedTileSampler.h"
#include "ProcessingInternal.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
namespace {
constexpr int dx[]={2,0,2,-2},dy[]={0,2,2,2};
struct Direction {
    std::array<double,4> signedContrast{};
    double energy=0,weight=0,squared=0;
    void Add(const std::array<double,4>& contrast,double w) {
        weight+=w;squared+=w*w;
        for(unsigned c=0;c<4;++c) {
            signedContrast[c]+=w*contrast[c];
            energy+=w*contrast[c]*contrast[c]*.25;
        }
    }
    std::uint8_t Agreement() const {
        if(weight*weight<1.8*squared||weight<=0)return 0;
        double repeated=0;
        for(double v:signedContrast)repeated=std::max(repeated,v*v/squared);
        // A noise-sized difference can change sign from frame to frame.
        // Large contradictory differences also veto borrowing (e.g. motion).
        const double excess=std::max(0.,repeated-2.)+std::max(0.,energy/weight-1.5);
        return static_cast<std::uint8_t>(std::lround(255./(1.+excess*excess)));
    }
};
struct Cell {std::array<double,4> value{},variance{};};
bool ReadCell(const PreparedSource& s,const Mfd::CfaLayout& layout,PreparedTileSampler& sampler,
    Mfd::RawCoordinate point,Cell& cell,std::string& error) {
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.x<0||point.y<0||
       point.x>s.frame.activeExtent.width-2||point.y>s.frame.activeExtent.height-2)return false;
    const int left=2*int(std::round(point.x*.5)),top=2*int(std::round(point.y*.5));
    for(unsigned c=0;c<4;++c) {
        const int x=left+(c&1),y=top+(c>>1);
        Mfd::SameCfaTapInput tap;
        if(!sampler.ReadSample(s.frame,x,y,tap,&error))return false;
        if(tap.sampleFlags||!std::isfinite(tap.normalizedSample)||tap.normalizedSample>=.95||
           !std::isfinite(tap.comparisonGain)||tap.comparisonGain<=0)return false;
        const auto& noise=s.noise.sites[unsigned(layout.SiteAt(x,y))];
        const double gain=tap.comparisonGain*s.scale;
        cell.value[c]=tap.normalizedSample*gain;
        cell.variance[c]=gain*gain*(noise.shotScale*std::max(0.,double(tap.normalizedSample))+
            noise.offsetVariance+(noise.quantizationIncluded?0.:noise.quantizationVariance));
        if(!std::isfinite(cell.value[c])||!std::isfinite(cell.variance[c])||cell.variance[c]<=0)return false;
    }
    return true;
}
}
std::array<float,4> BurstSpatialGuide::At(float x,float y) const {
    std::array<float,4> result{};
    if(cells.empty())return result;
    const float u=std::clamp((x-4.f)/stride,0.f,float(width-1));
    const float v=std::clamp((y-4.f)/stride,0.f,float(height-1));
    const unsigned ix=unsigned(u),iy=unsigned(v);
    for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i) {
        const auto& cell=cells[std::size_t(std::min(iy+j,height-1))*width+std::min(ix+i,width-1)];
        const float w=(i?u-ix:1-(u-ix))*(j?v-iy:1-(v-iy))/255.f;
        for(unsigned d=0;d<4;++d)result[d]+=w*cell[d];
    }
    return result;
}
bool MeasureSpatialGuide(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    if(!NeedsBurstSpatialGuide(request.recipe)||std::count_if(data.sources.begin(),data.sources.end(),
        [](const auto& s){return s.enabled;})<2)return true;
    ProcessingTimer timer(data.stageSeconds,"directional_evidence");
    if(request.reportProgress)request.reportProgress(.52,"Measuring directional agreement across captures");
    auto& guide=data.spatialGuide;guide.width=(data.width+7)/8;guide.height=(data.height+7)/8;
    const auto count=std::size_t(guide.width)*guide.height;
    const auto tile=data.sources[data.origin].frame.tileRawPixels;
    if(count*4+16ull*guide.width*sizeof(std::array<Direction,4>)+10ull*tile*tile*9>
       request.memoryBudgetBytes/4){error="Burst direction evidence exceeds the preparation memory budget.";return false;}
    guide.cells.resize(count);PreparedTileSampler sampler(data.directory,8);
    std::size_t supported=0;
    for(unsigned first=0;first<guide.height;first+=16) {
        const unsigned rows=std::min(16u,guide.height-first);
        std::vector<std::array<Direction,4>> stats(std::size_t(rows)*guide.width);
        for(const auto& source:data.sources) {
            if(!source.enabled)continue;
            Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
            const bool reference=&source==&data.sources[data.origin];
            // Traverse a source tile before advancing across a wide sensor.
            // A row-major sweep of the whole image evicts the same RAW tiles
            // between every two guide rows on a bounded sampler.
            for(unsigned left=0;left<guide.width;left+=16)for(unsigned y=0;y<rows;++y) {
                if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
                for(unsigned x=left;x<std::min(left+16,guide.width);++x) {
                    const double px=x*8.+4,py=(first+y)*8.+4;
                    const auto m=EvaluateCaptureCorrespondence(source,{px,py},reference);
                    if(m.confidence<=0)continue;
                    Cell center;
                    if(!ReadCell(source,layout,sampler,m.sourceRaw,center,error)){if(!error.empty())return false;continue;}
                    auto& directions=stats[std::size_t(y)*guide.width+x];
                    for(unsigned d=0;d<4;++d) {
                        const auto n=EvaluateCaptureCorrespondence(source,{px+dx[d],py+dy[d]},reference);
                        if(n.confidence<=0)continue;
                        // Reject folds or a CFA rounding change across a motion boundary.
                        if(std::abs((n.sourceRaw.x-m.sourceRaw.x)-dx[d])>.35||
                           std::abs((n.sourceRaw.y-m.sourceRaw.y)-dy[d])>.35)continue;
                        Cell neighbor;
                        if(!ReadCell(source,layout,sampler,{m.sourceRaw.x+dx[d],m.sourceRaw.y+dy[d]},neighbor,error)){
                            if(!error.empty())return false;continue;
                        }
                        std::array<double,4> contrast{};
                        for(unsigned c=0;c<4;++c)contrast[c]=(neighbor.value[c]-center.value[c])/
                            std::sqrt(center.variance[c]+neighbor.variance[c]);
                        directions[d].Add(contrast,std::min(m.confidence,n.confidence)*source.detailPreference);
                    }
                }
            }
        }
        for(std::size_t p=0;p<stats.size();++p) {
            auto& cell=guide.cells[std::size_t(first)*guide.width+p];
            for(unsigned d=0;d<4;++d)cell[d]=stats[p][d].Agreement();
            supported+=*std::max_element(cell.begin(),cell.end())>0;
        }
    }
    diagnostics.push_back("Burst direction agreement: "+std::to_string(supported)+" of "+std::to_string(count)+" spatial cells.");
    if(!supported)guide={};
    return true;
}
Observation SmoothBurstSample(const Observation* center,unsigned pitch,const std::array<float,4>& agreement) {
    Observation out=*center;
    const auto eligible=[](const Observation& o){return o.finite&&!o.clipped&&!o.localRejected&&
        o.headroom>0&&o.support>0&&o.measurementVariance>0&&o.uncertaintyVariance<=o.measurementVariance;};
    if(!eligible(out))return out;
    double total=1,value=out.value,sensorSd=std::sqrt(out.measurementVariance),modelSd=std::sqrt(out.uncertaintyVariance);
    for(unsigned d=0;d<4;++d)for(int sign:{-1,1}) {
        if(agreement[d]<=0)continue;
        const auto& n=center[sign*(dx[d]+std::ptrdiff_t(dy[d])*pitch)];
        if(!eligible(n))continue;
        const double diff=n.value-center->value;
        const double z2=diff*diff/(n.measurementVariance+center->measurementVariance);
        const double w=(d<2?.20:.12)*agreement[d]*std::exp(-z2)*std::min(1.,center->measurementVariance/n.measurementVariance);
        if(w<.001)continue;
        total+=w;value+=w*n.value;sensorSd+=w*std::sqrt(n.measurementVariance);
        modelSd+=w*std::sqrt(n.uncertaintyVariance);out.support=std::min(out.support,n.support);
    }
    if(total==1)return out;
    out.value=value/total;
    // Overlapping aligned footprints share taps. Use the correlated bound;
    // neighbouring pixels never add independent capture support.
    out.measurementVariance=std::pow(sensorSd/total,2);
    out.uncertaintyVariance=std::pow(modelSd/total,2);
    out.variance=out.measurementVariance+out.uncertaintyVariance;
    return out;
}
}
