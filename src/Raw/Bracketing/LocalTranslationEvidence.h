#pragma once
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Raw::Bracketing {
// Phase correlation can be ambiguous when a large part of the reference clips.
// Bidirectionally verified local matches can still establish a translation.
// Require spatial coverage and agreement before extending it into missing data.
inline bool RecoverLocalTranslation(const Mfd::LocalMotionGrid& grid,Mfd::RawCoordinate& translation) {
    if(!grid.valid||!grid.referenceRawExtent.width||!grid.referenceRawExtent.height||
       !grid.globalWarp.IsIdentity()) return false;
    std::vector<double> dx,dy;
    double minX=grid.referenceRawExtent.width,maxX=0;
    double minY=grid.referenceRawExtent.height,maxY=0;
    for(const auto& node:grid.nodes) {
        if(node.state!=Mfd::MotionNodeState::Structured||node.confidence<.5) continue;
        if(!std::isfinite(node.residualRaw.x)||!std::isfinite(node.residualRaw.y)) return false;
        dx.push_back(node.residualRaw.x);dy.push_back(node.residualRaw.y);
        minX=std::min(minX,node.centerRaw.x);maxX=std::max(maxX,node.centerRaw.x);
        minY=std::min(minY,node.centerRaw.y);maxY=std::max(maxY,node.centerRaw.y);
    }
    if(dx.size()<16||maxX-minX<.25*grid.referenceRawExtent.width||
       maxY-minY<.25*grid.referenceRawExtent.height) return false;
    const auto median=[](std::vector<double>& values) {
        const auto middle=values.begin()+values.size()/2;
        std::nth_element(values.begin(),middle,values.end());
        return *middle;
    };
    const Mfd::RawCoordinate candidate{median(dx),median(dy)};
    const double overlap=std::max(0.,1-std::abs(candidate.x)/grid.referenceRawExtent.width)*
        std::max(0.,1-std::abs(candidate.y)/grid.referenceRawExtent.height);
    if(overlap<.55) return false;
    for(const auto& node:grid.nodes) {
        if(node.state!=Mfd::MotionNodeState::Structured||node.confidence<.5) continue;
        // A single conflicting verified region defeats the global hypothesis.
        // Quarter-pixel agreement also excludes meaningful rotation or parallax.
        if(std::hypot(node.residualRaw.x-candidate.x,node.residualRaw.y-candidate.y)>.25) return false;
    }
    translation=candidate;
    return true;
}
}
