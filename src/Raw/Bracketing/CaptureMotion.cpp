#include "CaptureMotion.h"
#include "ProcessingInternal.h"
#include "Raw/MultiFrameDenoise/MotionJacobian.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
bool EvaluateCaptureMotion(const PreparedSource& source,Mfd::RawCoordinate point,
    const Mfd::LocalMotionOptions& options,Mfd::LocalMotionFieldSample& result,double* refinementConfidence,Mfd::RawMappingJacobian* jacobian) {
    if(jacobian)*jacobian={};
    if(refinementConfidence)*refinementConfidence=1;
    // Numeric consumers retain rejection reasons without allocating a message
    // for every sensor pixel. The public evaluator still supplies diagnostics.
    if(!source.localMotion||!Mfd::EvaluateLocalMotionField(*source.localMotion,point,options,result,nullptr,false,jacobian))return false;
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
    const auto correctionDerivative=[&]() {
        const auto derivative=[&](unsigned bx,unsigned by) {
            Mfd::RawMappingJacobian j;j.linear={0,0,0,0};
            const double u=x-bx,v=y-by,wx[2]={1-u,u},wy[2]={1-v,v};
            for(unsigned c=0;c<4;++c) {
                const auto& cell=field->cells[std::size_t(by+c/2)*field->width+bx+c%2];
                if(cell.confidence<=0)continue;
                const double dx=(c%2?1.:-1.)*wy[c/2]/field->spacing;
                const double dy=(c/2?1.:-1.)*wx[c%2]/field->spacing;
                j.linear[0]+=dx*cell.delta.x;j.linear[1]+=dy*cell.delta.x;
                j.linear[2]+=dx*cell.delta.y;j.linear[3]+=dy*cell.delta.y;
            }
            j.valid=Mfd::FiniteMappingJacobian(j);return j;
        };
        auto d=derivative(ix,iy);
        const bool left=ix>0&&x==double(ix),above=iy>0&&y==double(iy);
        for(unsigned c=1;c<4&&d.valid;++c) {
            if((c&1)&&!left)continue;if((c&2)&&!above)continue;
            const auto other=derivative(ix-((c&1)?1:0),iy-((c&2)?1:0));
            d.valid=other.valid&&Mfd::SameMappingJacobian(d,other);
        }
        // Refinement is zero outside its domain. At an interior domain edge
        // require continuity of both its value and outward normal derivative.
        const bool edgeX=(x==0&&point.x>0)||(x==field->width-1&&point.x<source.frame.activeExtent.width-1);
        const bool edgeY=(y==0&&point.y>0)||(y==field->height-1&&point.y<source.frame.activeExtent.height-1);
        if((edgeX||edgeY)&&(!Mfd::SameMappingDerivative(delta.x,0)||!Mfd::SameMappingDerivative(delta.y,0)))d.valid=false;
        if(edgeX&&(!Mfd::SameMappingDerivative(d.linear[0],0)||!Mfd::SameMappingDerivative(d.linear[2],0)))d.valid=false;
        if(edgeY&&(!Mfd::SameMappingDerivative(d.linear[1],0)||!Mfd::SameMappingDerivative(d.linear[3],0)))d.valid=false;
        return d;
    };
    Mfd::RawMappingJacobian correction;
    if(jacobian&&jacobian->valid)correction=correctionDerivative();
    if(mass<=0){
        if(jacobian&&jacobian->valid) {
            jacobian->valid=correction.valid;
            for(double value:correction.linear)
                if(!Mfd::SameMappingDerivative(value,0))jacobian->valid=false;
        }
        return true;
    }
    const auto mapped=Mfd::RawCoordinate{result.sourceRaw.x+delta.x,result.sourceRaw.y+delta.y};
    // At the refinement's source-domain switch the returned map can jump
    // back to the base field. Retain the sample but not a unique derivative.
    if(jacobian&&jacobian->valid&&(mapped.x==0||mapped.y==0||
       mapped.x==source.frame.activeExtent.width||mapped.y==source.frame.activeExtent.height))jacobian->valid=false;
    if(mapped.x<0||mapped.y<0||mapped.x>=source.frame.activeExtent.width||mapped.y>=source.frame.activeExtent.height)return true;
    if(jacobian&&jacobian->valid) {
        jacobian->valid=correction.valid;
        if(correction.valid)for(unsigned k=0;k<4;++k)jacobian->linear[k]+=correction.linear[k];
        jacobian->valid=jacobian->valid&&Mfd::FiniteMappingJacobian(*jacobian);
    }

    result.sourceRaw=mapped;result.residualRaw.x+=delta.x;result.residualRaw.y+=delta.y;
    // Interpolating estimated corrections adds model uncertainty, not sensor noise.
    result.covarianceRaw.xxRawPixelsSquared+=risk;
    result.covarianceRaw.yyRawPixelsSquared+=risk;
    result.alignmentConfidence=std::min(result.alignmentConfidence,confidence+1-mass);
    if(refinementConfidence)*refinementConfidence=std::clamp(confidence+1-mass,0.,1.);
    return true;
}
}
