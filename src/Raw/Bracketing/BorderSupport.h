#pragma once

#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include "Raw/MultiFrameDenoise/SameCfaSampler.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {

// Extend the *motion estimate*, not the sensor measurements. The outer nodes
// carry the nearest supported residual and extra uncertainty. Sampling still
// rejects coordinates outside the real source plane. Existing interior cells
// and forward/backward closure decisions remain unchanged.
inline void ExtendMotionBoundary(Mfd::LocalMotionGrid& grid) {
    if(!grid.valid||grid.width<2||grid.height<2||grid.nodes.size()!=
       static_cast<std::size_t>(grid.width)*grid.height) return;
    auto original=std::move(grid.nodes);
    const auto width=grid.width,height=grid.height;
    grid.width+=2;grid.height+=2;
    grid.originRawX-=grid.spacingRawX;grid.originRawY-=grid.spacingRawY;
    grid.nodes.resize(static_cast<std::size_t>(grid.width)*grid.height);
    grid.structuredCount=grid.flatSafeCount=grid.rejectedCount=0;
    for(unsigned y=0;y<grid.height;++y) for(unsigned x=0;x<grid.width;++x) {
        const auto sx=std::clamp<int>(static_cast<int>(x)-1,0,width-1);
        const auto sy=std::clamp<int>(static_cast<int>(y)-1,0,height-1);
        auto& node=grid.nodes[static_cast<std::size_t>(y)*grid.width+x];
        node=original[static_cast<std::size_t>(sy)*width+sx];
        node.gridX=x;node.gridY=y;
        node.centerRaw={grid.originRawX+x*grid.spacingRawX,grid.originRawY+y*grid.spacingRawY};
        if(x==0||y==0||x+1==grid.width||y+1==grid.height) {
            node.covarianceRaw.xxRawPixelsSquared+=.25;
            node.covarianceRaw.yyRawPixelsSquared+=.25;
        }
        if(node.state==Mfd::MotionNodeState::Structured) ++grid.structuredCount;
        else if(node.state==Mfd::MotionNodeState::FlatSafe) ++grid.flatSafeCount;
        else ++grid.rejectedCount;
    }
}

// Interior uses Keys bicubic. At a physical image edge use the available
// bilinear footprint, without reflected/repeated measurements being counted
// as independent evidence. Integer positions are exact, including corners.
inline bool BuildSupportedFootprint(Mfd::CfaPlaneCoordinate coordinate,
    Mfd::PixelExtent extent,Mfd::KeysBicubicFootprint& footprint) {
    if(!Mfd::BuildKeysBicubicFootprint(coordinate,footprint)) return false;
    if(Mfd::ValidateKeysBicubicFootprint(footprint,extent)) return true;
    if(extent.width<2||extent.height<2||coordinate.x<0||coordinate.y<0||
       coordinate.x>extent.width-1||coordinate.y>extent.height-1) return false;
    footprint={};footprint.coordinate=coordinate;footprint.coefficientSum=1;
    const auto x=std::min<std::int64_t>(std::floor(coordinate.x),extent.width-2);
    const auto y=std::min<std::int64_t>(std::floor(coordinate.y),extent.height-2);
    const double u=coordinate.x-x,v=coordinate.y-y;
    const std::array<double,4> weights={(1-u)*(1-v),u*(1-v),(1-u)*v,u*v};
    const std::array<double,4> dx={v-1,1-v,-v,v};
    const std::array<double,4> dy={u-1,-u,1-u,u};
    for(unsigned i=0;i<4;++i) {
        footprint.taps[i]={x+i%2,y+i/2,coordinate.site};
        footprint.coefficients[i]=weights[i];
        footprint.derivativeXPlane[i]=dx[i];
        footprint.derivativeYPlane[i]=dy[i];
    }
    for(unsigned i=4;i<footprint.taps.size();++i) footprint.taps[i]=footprint.taps[0];
    return true;
}

} // namespace Raw::Bracketing
