#include "CaptureMotion.h"
#include "ProcessingInternal.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
bool EvaluateCaptureMotion(const PreparedSource& source,Mfd::RawCoordinate point,
    const Mfd::LocalMotionOptions& options,Mfd::LocalMotionFieldSample& result,double* refinementConfidence) {
    if(refinementConfidence)*refinementConfidence=1;
    if(!source.localMotion||!Mfd::EvaluateLocalMotionField(*source.localMotion,point,options,result))return false;
    const auto* field=source.refinedMotion.get();
    if(!field||field->width<2||field->height<2||field->spacing<=0||
       field->cells.size()!=std::size_t(field->width)*field->height)return true;
    const double x=point.x/field->spacing,y=point.y/field->spacing;
    if(x<0||y<0||x>field->width-1||y>field->height-1)return true;
    const unsigned ix=std::min(unsigned(x),field->width-2),iy=std::min(unsigned(y),field->height-2);
    const double u=x-ix,v=y-iy;
    const double weights[4]={(1-u)*(1-v),u*(1-v),(1-u)*v,u*v};
    Mfd::RawCoordinate delta{};double risk=0,confidence=0,mass=0;
    for(unsigned c=0;c<4;++c) {
        const auto& cell=field->cells[std::size_t(iy+c/2)*field->width+ix+c%2];
        if(cell.confidence<=0)continue;
        const double w=weights[c];mass+=w;confidence+=w*cell.confidence;
        delta.x+=w*cell.delta.x;delta.y+=w*cell.delta.y;
        risk+=w*(std::max(cell.covariance.xxRawPixelsSquared,cell.covariance.yyRawPixelsSquared)+
            std::abs(cell.covariance.xyRawPixelsSquared)+cell.delta.x*cell.delta.x+cell.delta.y*cell.delta.y);
    }
    if(mass<=0)return true;
    const auto mapped=Mfd::RawCoordinate{result.sourceRaw.x+delta.x,result.sourceRaw.y+delta.y};
    if(mapped.x<0||mapped.y<0||mapped.x>=source.frame.activeExtent.width||mapped.y>=source.frame.activeExtent.height)return true;
    result.sourceRaw=mapped;result.residualRaw.x+=delta.x;result.residualRaw.y+=delta.y;
    // Interpolating estimated corrections adds model uncertainty, not sensor noise.
    result.covarianceRaw.xxRawPixelsSquared+=risk;
    result.covarianceRaw.yyRawPixelsSquared+=risk;
    result.alignmentConfidence=std::min(result.alignmentConfidence,confidence+1-mass);
    if(refinementConfidence)*refinementConfidence=std::clamp(confidence+1-mass,0.,1.);
    return true;
}
}
