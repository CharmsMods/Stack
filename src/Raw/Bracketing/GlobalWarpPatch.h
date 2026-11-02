#pragma once
#include "BurstNoise.h"
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <array>

namespace Raw::Bracketing {
// A global warp that fits one surface may still be safe elsewhere. Compare
// the actual radiance of each proposed patch at a noise-reduced scale, using
// the local same-color Haar noise measured in the original captures.
inline bool ReadWarpPatchSample(const Mfd::CfaPlanePyramid& pyramid,Mfd::CfaSite site,
    Mfd::RawCoordinate raw,double& signal,double& detail) {
    const auto& levels=pyramid.planes[static_cast<unsigned>(site)];
    if(levels.size()<3)return false;
    const auto& base=levels[0];
    const auto& smooth=levels[2];
    const auto plane=pyramid.layout.RawToPlane(raw,site);
    const double x=plane.x*.25,y=plane.y*.25;
    if(x<0||y<0||x+1>=smooth.extent.width||y+1>=smooth.extent.height)return false;
    const auto ix=std::size_t(x),iy=std::size_t(y);
    const auto bx=std::size_t(plane.x),by=std::size_t(plane.y);
    if(bx+1>=base.extent.width||by+1>=base.extent.height)return false;
    signal=0;detail=0;
    const double u=x-ix,v=y-iy;
    for(unsigned c=0;c<4;++c) {
        const auto s=(iy+c/2)*smooth.extent.width+ix+c%2;
        const auto b=(by+c/2)*base.extent.width+bx+c%2;
        if(!smooth.validMask[s]||!base.validMask[b])return false;
        signal+=smooth.signal[s]*(c%2?u:1-u)*(c/2?v:1-v);
        detail+=base.signal[b]*(c==0||c==3?.5:-.5);
    }
    return std::isfinite(signal)&&std::isfinite(detail);
}

inline bool GlobalWarpPatchAgrees(const Mfd::CfaPlanePyramid& reference,const Mfd::CfaPlanePyramid& source,
    const Mfd::LocalMotionGrid& grid,Mfd::RawCoordinate center,double scale,double relativeScaleSigma) {
    unsigned verified=0;
    for(unsigned c=0;c<4;++c) {
        const auto site=static_cast<Mfd::CfaSite>(c);
        std::vector<double> details;
        details.reserve(49);
        struct Pair {double a=0,b=0;bool valid=false;};
        std::array<Pair,49> samples{};
        double squared=0,peak=0,signal=0,energyA=0,energyB=0,cross=0;
        unsigned contrasts=0;
        for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
            const Mfd::RawCoordinate point{center.x+x*grid.spacingRawX/6,center.y+y*grid.spacingRawY/6};
            double a,b,da,db;
            if(!ReadWarpPatchSample(reference,site,point,a,da) ||
               !ReadWarpPatchSample(source,site,grid.globalWarp.Map(point),b,db))continue;
            b*=scale;
            signal+=std::max(std::abs(a),std::abs(b));
            const int i=(y+3)*7+x+3;
            samples[i]={a,b,true};
            // Look for the same detail after removing the local brightness
            // slope. Similar average radiance can conceal displaced texture
            // such as the pattern on two folds of a curtain.
            const auto contrast=[&](int step) {
                const auto& m=samples[i-step];const auto& l=samples[i-2*step];
                if(!m.valid||!l.valid)return;
                const double ca=a-2*m.a+l.a,cb=b-2*m.b+l.b;
                energyA+=ca*ca;energyB+=cb*cb;cross+=ca*cb;++contrasts;
            };
            if(x>=-1)contrast(1);
            if(y>=-1)contrast(7);
            // Exposure uncertainty is common to a patch; it does not shrink
            // with the number of pixels. Cap it so an uncertain exposure fit
            // cannot by itself certify a geometric match.
            const double residual=std::max(0.,std::abs(a-b)-std::min(.03,2*relativeScaleSigma)*std::abs(b));
            squared+=residual*residual;peak=std::max(peak,residual);
            details.push_back(da-scale*db);
        }
        if(details.size()<32)continue;
        BurstNoiseBlock noise;
        MeasureBurstNoiseBlock(details,0,noise);
        // Twice-filtered CFA signal has about one eighth the standard
        // deviation of a native tap. Allow interpolation and correlated noise,
        // but a displaced edge must also pass the individual residual bound.
        const double sigma=std::max(1./65535.,std::sqrt(noise.variance)*.16);
        if(squared>details.size()*9*sigma*sigma || peak>6*sigma)return false;
        const double contrastFloor=std::max(2*sigma,.02*signal/details.size());
        if(std::max(energyA,energyB)>contrasts*contrastFloor*contrastFloor &&
           (cross<=0 || cross<.97*std::sqrt(energyA*energyB)))return false;
        ++verified;
    }
    return verified>=2;
}
}
