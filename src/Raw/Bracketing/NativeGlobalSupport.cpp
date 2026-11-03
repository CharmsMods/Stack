#include "NativeGlobalSupport.h"
#include "ParallelProcessing.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
namespace {
constexpr auto invalidAlignment=Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::AlignmentInvalid);

// Four-by-four Bayer cells per decision. The overlapping test footprint
// includes a displacement margin around the entire output block.
constexpr int blockPixels=8;
struct Moments {
    double sum=0,squares=0,variance=0;
    void Add(double signal,double noise) {
        sum+=signal;squares+=signal*signal;variance+=noise;
    }
    double Structure(int n)const {
        return std::max(0.,(squares-sum*sum/n)/(n-1)-variance/n);
    }
};

bool GlobalCandidate(const PreparedSource& source,Mfd::RawCoordinate center) {
    const auto& grid=*source.localMotion;
    const auto ix=std::llround((center.x-grid.originRawX)/grid.spacingRawX);
    const auto iy=std::llround((center.y-grid.originRawY)/grid.spacingRawY);
    if(ix<0||iy<0||ix>=grid.width||iy>=grid.height)return false;
    const auto& node=grid.nodes[std::size_t(iy)*grid.width+ix];
    if(node.state==Mfd::MotionNodeState::Rejected) {
        using R=Mfd::MotionNodeRejectReason;
        return node.rejectReason==R::AmbiguousMatch||node.rejectReason==R::NoDiscreteCandidate||
            node.rejectReason==R::UnobservableUnsafe||node.rejectReason==R::ForwardBackwardUnavailable||
            node.rejectReason==R::SubpixelFailure;
    }
    const auto global=source.alignment.Map(node.centerRaw);
    const auto local=grid.globalWarp.Map(node.centerRaw);
    return std::hypot(local.x+node.residualRaw.x-global.x,local.y+node.residualRaw.y-global.y)
        +node.positionalSigmaRaw<=kGlobalCheckRadiusRaw;
}

double PatchConfidence(const PreparedSource& source,const Mfd::CfaPlanePyramid& reference,
    const Mfd::CfaPlanePyramid& alternate,int left,int top) {
    if(!GlobalCandidate(source,{double(left+blockPixels/2),double(top+blockPixels/2)}))return 0;
    double worst=0;
    for(unsigned c=0;c<4;++c) {
        const auto site=static_cast<Mfd::CfaSite>(c);
        const auto offset=reference.layout.OffsetFor(site);
        const auto& ref=reference.planes[c].front();const auto& alt=alternate.planes[c].front();
        Moments a,b;double squaredError=0;
        int n=0;
        for(int dy=-int(kGlobalCheckRadiusRaw);dy<blockPixels+kGlobalCheckRadiusRaw;dy+=4)
            for(int dx=-int(kGlobalCheckRadiusRaw);dx<blockPixels+kGlobalCheckRadiusRaw;dx+=4) {
                const int x=left+dx,y=top+dy;
                if(x<0||y<0||unsigned(x/2)>=ref.extent.width||unsigned(y/2)>=ref.extent.height)return 0;
                const auto ri=std::size_t(y/2)*ref.extent.width+x/2;
                if(!ref.validMask[ri])return 0;
                const auto mapped=source.alignment.Map({double(x)+offset.x,double(y)+offset.y});
                const auto plane=alternate.layout.RawToPlane(mapped,site);
                if(!(plane.x>=0&&plane.y>=0&&plane.x+1<alt.extent.width&&plane.y+1<alt.extent.height))return 0;
                const int ax=int(plane.x),ay=int(plane.y);
                const double u=plane.x-ax,v=plane.y-ay;
                double signal=0,noise=0;
                for(unsigned tap=0;tap<4;++tap) {
                    const double w=(tap%2?u:1-u)*(tap/2?v:1-v);
                    if(!w)continue;
                    const auto ai=(std::size_t(ay)+tap/2)*alt.extent.width+ax+tap%2;
                    if(!alt.validMask[ai])return 0;
                    signal+=w*alt.signal[ai];noise+=w*w*alt.variance[ai];
                }
                signal*=source.scale;noise*=source.scale*source.scale;
                a.Add(ref.signal[ri],ref.variance[ri]);b.Add(signal,noise);
                const double diff=ref.signal[ri]-signal;
                squaredError+=diff*diff;++n;
            }
        const double noise=std::max(1e-12,(a.variance+b.variance)/n);
        // Calibration uncertainty is common to the patch, never divided by n.
        const double calibration=std::min(.03,std::sqrt(source.scaleVariance)/source.scale)*std::abs(b.sum/n);
        const double bias=std::max(0.,std::abs(a.sum-b.sum)/n-calibration);
        // Test agreement and displacement risk independently. Even a perfect
        // mean match cannot authorize the transfer of a contrasting edge.
        const double mismatch=std::max(bias*bias/(noise*2/n),
            squaredError/(n*(noise+calibration*calibration)));
        worst=std::max({worst,mismatch,a.Structure(n)/noise,b.Structure(n)/noise});
        if(!std::isfinite(squaredError)||!std::isfinite(noise)||!std::isfinite(bias)||worst>=16)return 0;
    }
    return Mfd::FlatTopQuinticGate(std::sqrt(worst),1.5,4.);
}
}

bool VerifyNativeGlobalSupport(const ProcessingRequest& request,const PreparedSource& source,
    const Mfd::CfaPlanePyramid& reference,const Mfd::CfaPlanePyramid& alternate,Mfd::ReliabilityMap& map) {
    if(!source.globalAlignmentAccepted||!source.localMotion||!source.localMotion->valid||
        !(source.localMotion->spacingRawX>0&&source.localMotion->spacingRawY>0)||
        !(source.scale>0&&std::isfinite(source.scale)&&source.scaleVariance>=0&&std::isfinite(source.scaleVariance)))return true;
    const unsigned step=2*map.cellStrideBayerCells;
    if(!step||blockPixels%step)return true;
    const unsigned block=blockPixels/step,w=static_cast<unsigned>(map.cellExtent.width),h=static_cast<unsigned>(map.cellExtent.height);
    return ProcessParallelIndices((h+block-1)/block,request.workerCount,request.shouldCancel,[&](std::size_t row) {
        for(unsigned x=0;x<w;x+=block) {
            const unsigned y=unsigned(row)*block;
            const auto visit=[&](auto action) {
                for(unsigned yy=y;yy<std::min(y+block,h);++yy)
                    for(unsigned xx=x;xx<std::min(x+block,w);++xx)action(map.cells[std::size_t(yy)*w+xx]);
            };
            bool needed=false;visit([&](const auto& cell){needed|=cell.rejectionBits==invalidAlignment;});
            if(!needed)continue;
            const double q=PatchConfidence(source,reference,alternate,x*step,y*step);
            if(q>0)visit([&](auto& cell){
                if(cell.rejectionBits==invalidAlignment){cell.rejectionBits|=kNativeGlobalVerified;cell.reliability=q;}
            });
        }
        return true;
    });
}
}
