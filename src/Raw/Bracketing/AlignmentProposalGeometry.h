#pragma once
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing::ProposalGeometry {

inline bool InDomain(Mfd::RawCoordinate point,Mfd::PixelExtent extent) {
    return std::isfinite(point.x)&&std::isfinite(point.y)&&point.x>=0&&point.y>=0&&
        point.x<double(extent.width)&&point.y<double(extent.height);
}

// This inverse only transports a search hint. It never establishes geometry,
// transfers confidence, or supplies reconstruction pixels. A failed inverse
// omits that hint; the ordinary native candidates remain available.
//
// Solve A(x)=target by bounded, backtracked Newton steps through the *accepted*
// origin-to-anchor map A. A^-1 is not approximated by reversing learned flow.
// The map evaluator supplies its actual analytic derivative and returns false
// for unsupported geometry. A derivative switch at a grid knot is unavailable.
template<class EvaluateMap>
bool InvertReference(const EvaluateMap& evaluate,Mfd::PixelExtent originExtent,
    Mfd::PixelExtent anchorExtent,const Mfd::AffineModel& affineInverse,
    Mfd::RawCoordinate target,Mfd::RawCoordinate& result) {
    if(!InDomain(target,anchorExtent))return false;
    auto current=affineInverse.Map(target);
    if(!InDomain(current,originExtent))return false;
    const auto initial=current;
    // Bounds limit optional work, not native motion acceptance. An anchor
    // farther from its affine initializer simply contributes no learned hint.
    constexpr unsigned iterations=8,backtracks=4;
    constexpr double toleranceRaw=.01,maximumStepRaw=32,maximumTravelRaw=128;
    constexpr double maximumCondition=64;
    for(unsigned iteration=0;iteration<=iterations;++iteration) {
        Mfd::RawCoordinate mapped;Mfd::RawMappingJacobian jacobian;
        if(!evaluate(current,mapped,&jacobian)||!jacobian.valid||
           !InDomain(mapped,anchorExtent))return false;
        const auto& j=jacobian.linear;
        if(!std::all_of(j.begin(),j.end(),[](double v){return std::isfinite(v);}))return false;
        const double scale=std::max({std::abs(j[0]),std::abs(j[1]),std::abs(j[2]),std::abs(j[3])});
        if(!(scale>0))return false;
        const double a=j[0]/scale,b=j[1]/scale,c=j[2]/scale,d=j[3]/scale;
        const double determinant=a*d-b*c;
        // For positive determinant, ||J||_F^2/det(J)=kappa+1/kappa.
        // A reflection/fold, singular map or poorly conditioned local inverse
        // is not a suitable way to transport a proposal across coordinates.
        if(!(determinant>0)||(a*a+b*b+c*c+d*d)/determinant>
            maximumCondition+1/maximumCondition)return false;
        const double rx=mapped.x-target.x,ry=mapped.y-target.y;
        const double residual=std::hypot(rx,ry);
        if(residual<=toleranceRaw){result=current;return true;}
        if(iteration==iterations)return false;
        Mfd::RawCoordinate step{(d*(rx/scale)-b*(ry/scale))/determinant,
            (-c*(rx/scale)+a*(ry/scale))/determinant};
        const double length=std::hypot(step.x,step.y);
        if(!std::isfinite(length)||length==0)return false;
        if(length>maximumStepRaw){step.x*=maximumStepRaw/length;step.y*=maximumStepRaw/length;}
        bool advanced=false;
        for(unsigned trial=0;trial<backtracks;++trial) {
            const Mfd::RawCoordinate next{current.x-step.x,current.y-step.y};
            Mfd::RawCoordinate nextMapped;
            if(InDomain(next,originExtent)&&std::hypot(next.x-initial.x,next.y-initial.y)<=maximumTravelRaw&&
               evaluate(next,nextMapped,nullptr)&&InDomain(nextMapped,anchorExtent)&&
               std::hypot(nextMapped.x-target.x,nextMapped.y-target.y)<residual) {
                current=next;advanced=true;break;
            }
            step.x*=.5;step.y*=.5;
        }
        if(!advanced)return false;
    }
    return false;
}
} // namespace Raw::Bracketing::ProposalGeometry
