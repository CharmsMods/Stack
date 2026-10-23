#pragma once

#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"
#include <cmath>
#include <limits>

namespace Raw::Bracketing {

// A same-CFA least-squares slope over a 9x9 raw neighborhood. This guide
// controls registration risk only; output samples are never filtered here.
inline bool BuildSceneGradients(const ProcessingRequest& request,const PreparedDataset& data,
    const Mfd::PreparedRawTile& tile, std::vector<Mfd::RawSignalGradient>& gradients,
    std::string& error,PreparedTileSampler& sampler) {
    const auto& source=data.sources[data.origin];
    const int width=static_cast<int>(tile.extent.width),height=static_cast<int>(tile.extent.height);
    const int stride=width+8;
    std::vector<double> signal(static_cast<std::size_t>(stride)*(height+8));
    std::vector<unsigned char> valid(signal.size());
    for(int y=-4;y<height+4;++y) {
      if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
      for(int x=-4;x<width+4;++x) {
        const auto rx=static_cast<std::int64_t>(tile.originX)+x;
        const auto ry=static_cast<std::int64_t>(tile.originY)+y;
        if(rx<0||ry<0||rx>=source.frame.activeExtent.width||ry>=source.frame.activeExtent.height) continue;
        Mfd::SameCfaTapInput tap;
        if(!sampler.ReadSample(source.frame,rx,ry,tap,&error)) return false;
        const auto i=static_cast<std::size_t>(y+4)*stride+x+4;
        signal[i]=tap.normalizedSample*tap.comparisonGain;
        valid[i]=std::isfinite(signal[i])&&tap.sampleFlags==0;
      }
    }
    Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
    gradients.resize(static_cast<std::size_t>(width)*height);
    for(int y=0;y<height;++y) {
      if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
      for(int x=0;x<width;++x) {
        double gx=0,gy=0,mean=0;unsigned count=0;
        const auto rx=static_cast<std::int64_t>(tile.originX)+x,ry=static_cast<std::int64_t>(tile.originY)+y;
        const bool border=rx<4||ry<4||rx+4>=source.frame.activeExtent.width||ry+4>=source.frame.activeExtent.height;
        double sx=0,sy=0,sxx=0,sxy=0,syy=0;unsigned available=0;
        for(int dy=-4;dy<=4;dy+=2) for(int dx=-4;dx<=4;dx+=2) {
            if(border&&rx+dx>=0&&ry+dy>=0&&rx+dx<source.frame.activeExtent.width&&ry+dy<source.frame.activeExtent.height)
                ++available;
            const auto i=static_cast<std::size_t>(y+dy+4)*stride+x+dx+4;
            if(!valid[i]) continue;
            gx+=dx*signal[i];gy+=dy*signal[i];mean+=signal[i];++count;
            if(border){sx+=dx;sy+=dy;sxx+=dx*dx;sxy+=dx*dy;syy+=dy*dy;}
        }
        // Interior arithmetic stays unchanged. At a physical image boundary,
        // fit a plane to the available real same-CFA samples and propagate the
        // larger slope-noise variance. Missing/clipped interior evidence still
        // uses the original unknown-gradient fallback.
        auto& out=gradients[static_cast<std::size_t>(y)*width+x];
        out={std::numeric_limits<double>::quiet_NaN(),0};
        double gradientNoiseFactor=2./200.;
        if(count==25){gx/=200.;gy/=200.;mean/=25.;}
        else {
            if(!border||count!=available||count<6)continue;
            mean/=count;
            sxx-=sx*sx/count;sxy-=sx*sy/count;syy-=sy*sy/count;
            const double determinant=sxx*syy-sxy*sxy;
            if(!(determinant>1e-12))continue;
            const double bx=gx-sx*mean,by=gy-sy*mean;
            gx=(syy*bx-sxy*by)/determinant;gy=(sxx*by-sxy*bx)/determinant;
            gradientNoiseFactor=(sxx+syy)/determinant;
        }
        const auto site=static_cast<std::size_t>(layout.SiteAt(tile.originX+x,tile.originY+y));
        const auto& profile=source.noise.sites[site];
        const auto center=static_cast<std::size_t>(y)*width+x;
        Mfd::GainPropagatedVariance variance;
        if(!Mfd::PropagateKnownGainVariance(profile,tile.comparisonGain[center],
            std::max(0.0,mean),source.frame.calibration.usableSpanByCfaSite[site],variance)) continue;
        // Interior slope variance is V/200 per axis; the one-sided fit uses
        // the trace of its inverse coordinate-moment matrix instead.
        // Subtract that expected noise energy before charging alignment risk.
        const double energy=gx*gx+gy*gy;
        const double noiseEnergy=count==25?2*variance.variance/200.:gradientNoiseFactor*variance.variance;
        const double retained=energy>0?std::sqrt(std::max(0.0,energy-noiseEnergy)/energy):0;
        out={gx*retained,gy*retained};
      }
    }
    return true;
}

inline bool BuildSceneGradients(const ProcessingRequest& request,const PreparedDataset& data,
    const Mfd::PreparedRawTile& tile,std::vector<Mfd::RawSignalGradient>& gradients,
    std::string& error) {
    PreparedTileSampler sampler(data.directory,16);
    return BuildSceneGradients(request,data,tile,gradients,error,sampler);
}

} // namespace Raw::Bracketing
