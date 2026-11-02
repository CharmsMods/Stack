#pragma once
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
// A lone verified patch measures only its own footprint. Between patches
// interpolate inside the triangle/rectangle of verified centers. Extending a
// single displacement across an empty cell can move a foreground silhouette
// by the full parallax of a different surface.
inline bool HasLocalWarpSupport(const Mfd::LocalMotionGrid& grid,Mfd::RawCoordinate point,double patchRadiusRaw) {
    if(!grid.rejectedCount)return true;
    if(grid.width<2||grid.height<2||grid.spacingRawX<=0||grid.spacingRawY<=0)return false;
    const double x=(point.x-grid.originRawX)/grid.spacingRawX;
    const double y=(point.y-grid.originRawY)/grid.spacingRawY;
    if(x<0||y<0||x>grid.width-1||y>grid.height-1)return false;
    const auto ix=std::min(static_cast<unsigned>(x),grid.width-2);
    const auto iy=std::min(static_cast<unsigned>(y),grid.height-2);
    const double u=x-ix,v=y-iy;
    unsigned supported=0,missing=0;
    for(unsigned c=0;c<4;++c) {
        const auto& node=grid.nodes[std::size_t(iy+c/2)*grid.width+ix+c%2];
        if(node.state==Mfd::MotionNodeState::Rejected||node.confidence<=0) {missing=c;continue;}
        if(std::abs(point.x-node.centerRaw.x)<=patchRadiusRaw &&
           std::abs(point.y-node.centerRaw.y)<=patchRadiusRaw)return true;
        ++supported;
    }
    if(supported==4)return true;
    if(supported!=3)return false;
    // In unit cell coordinates the opposite diagonal is one Manhattan unit
    // away from the missing corner.
    return std::abs(u-double(missing%2))+std::abs(v-double(missing/2))>=1.;
}
}
