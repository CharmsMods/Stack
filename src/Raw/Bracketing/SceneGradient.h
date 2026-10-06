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
    std::string& error) {
    const auto& source=data.sources[data.origin];
    const int width=static_cast<int>(tile.extent.width),height=static_cast<int>(tile.extent.height);
    const int stride=width+8;
    std::vector<double> signal(static_cast<std::size_t>(stride)*(height+8));
    std::vector<unsigned char> valid(signal.size());
    PreparedTileSampler sampler(data.directory,16);
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
        for(int dy=-4;dy<=4;dy+=2) for(int dx=-4;dx<=4;dx+=2) {
            const auto i=static_cast<std::size_t>(y+dy+4)*stride+x+dx+4;
            if(!valid[i]) continue;
            gx+=dx*signal[i];gy+=dy*signal[i];mean+=signal[i];++count;
        }
        // Incomplete footprints use a conservative unknown gradient. Their
        // risk is handled by the original sampler instead of inventing detail.
        auto& out=gradients[static_cast<std::size_t>(y)*width+x];
        out={std::numeric_limits<double>::quiet_NaN(),0};
        if(count!=25) continue;
        gx/=200.;gy/=200.;mean/=25.;
        const auto site=static_cast<std::size_t>(layout.SiteAt(tile.originX+x,tile.originY+y));
        const auto& profile=source.noise.sites[site];
        const auto center=static_cast<std::size_t>(y)*width+x;
        Mfd::GainPropagatedVariance variance;
        if(!Mfd::PropagateKnownGainVariance(profile,tile.comparisonGain[center],
            std::max(0.0,mean),source.frame.calibration.usableSpanByCfaSite[site],variance)) continue;
        // Each fitted slope has noise variance V / sum(dx^2) = V/200.
        // Subtract that expected noise energy before charging alignment risk.
        const double energy=gx*gx+gy*gy;
        const double retained=energy>0?std::sqrt(std::max(0.0,energy-2*variance.variance/200.)/energy):0;
        out={gx*retained,gy*retained};
      }
    }
    return true;
}

} // namespace Raw::Bracketing
