#pragma once

namespace Raw::Bracketing::TemporalShaders {

// Scalar counterpart: PartialCfaReconstruction.cpp / LocalAffineFit.h. This
// fragment follows Common and the Keys kernel declarations. It only reads the
// existing source footprint; no additional frame-sized GPU storage is needed.
inline const char* PartialCfaReconstruction=R"GLSL(
struct PartialCfaResult {
    double value,variance,gx,gy,dark,dn,model,headroom;
};
bool reconstructPartialCfa(int site,ivec2 base,dvec2 plane,ivec2 parity,
    double scale,double pilot,Mapping mapping,out PartialCfaResult result) {
    result=PartialCfaResult(0.LF,0.LF,0.LF,0.LF,0.LF,0.LF,0.LF,0.LF);
    double total=0.LF,sx=0.LF,sy=0.LF,sxx=0.LF,sxy=0.LF,syy=0.LF;
    double sv=0.LF,sxv=0.LF,syv=0.LF,centralMass=0.LF,centralVariance=0.LF;
    double safety=0.LF,magnitude=0.LF;
    int count=0;bool missing=false,valueComplete=true;
    for(int k=0;k<16;++k) {
        ivec2 p=base+ivec2(k%4-1,k/4-1);dvec2 d=dvec2(p)-plane;
        vec4 tap=tapAt(p*2+parity);if(badTap(tap))return false;
        double gain=scale*double(tap.y),v=gain*double(tap.x);
        double variance=noiseVariance(site,gain,pilot);
        if(!finiteValue(gain)||gain<=0.LF||!finiteValue(v)||
            !finiteValue(variance)||variance<0.LF)return false;
        bool clipped=(uint(tap.z)&9u)!=0u;
        double central=max(0.LF,1.LF-abs(d.x))*max(0.LF,1.LF-abs(d.y));
        // Every positive bilinear corner remains a real measurement. The fit
        // cannot claim a missing central sample or extend past observed data.
        if(central>0.LF&&clipped)return false;
        centralMass+=central;centralVariance+=central*central*variance;
        double original=kernel(d.x)*kernel(d.y);
        magnitude+=abs(original);
        safety+=abs(original)*(clipped?0.LF:captureHeadroom(double(tap.x),gain,pilot));
        if(clipped){missing=true;if(original!=0.LF)valueComplete=false;continue;}
        double radius=1.LF+.5LF*dot(d,d);
        double weight=1.LF/(radius*radius*max(1e-12LF,variance));
        total+=weight;sx+=weight*d.x;sy+=weight*d.y;
        sxx+=weight*d.x*d.x;sxy+=weight*d.x*d.y;syy+=weight*d.y*d.y;
        sv+=weight*v;sxv+=weight*d.x*v;syv+=weight*d.y*v;++count;
    }
    if(!missing||count<6||abs(centralMass-1.LF)>1e-12LF||magnitude<=0.LF||
        !finiteValue(total)||total<=0.LF)return false;
    double mx=sx/total,my=sy/total,mv=sv/total;
    double a=sxx/total-mx*mx,b=sxy/total-mx*my,c=syy/total-my*my;
    double trace=a+c,determinant=a*c-b*b;
    if(!finiteValue(trace)||!finiteValue(determinant)||a<=0.LF||c<=0.LF||
        determinant<=0.LF||determinant<1e-4LF*trace*trace)return false;
    double ixx=c/determinant,ixy=-b/determinant,iyy=a/determinant;
    double vx=sxv/total-mx*mv,vy=syv/total-my*mv;
    double slopeX=ixx*vx+ixy*vy,slopeY=ixy*vx+iyy*vy;
    double fitted=mv-slopeX*mx-slopeY*my;
    if(!finiteValue(fitted)||!finiteValue(slopeX)||!finiteValue(slopeY))return false;
    double normalizedResidual=0.LF,weightedResidual=0.LF,weightedNoise=0.LF;
    double coefficientMagnitude=0.LF,gradientNoiseRisk=0.LF;
    double sum=0.LF,sumDx=0.LF,sumDy=0.LF;
    for(int k=0;k<16;++k) {
        ivec2 p=base+ivec2(k%4-1,k/4-1);dvec2 d=dvec2(p)-plane;
        vec4 tap=tapAt(p*2+parity);
        double gain=scale*double(tap.y),v=gain*double(tap.x);
        double variance=noiseVariance(site,gain,pilot),prediction=fitted+slopeX*d.x+slopeY*d.y;
        if((uint(tap.z)&9u)!=0u) {
            // Clipped values constrain the model from below, never its mean.
            double deficit=v-prediction;
            if(deficit>0.LF&&deficit*deficit>16.LF*max(1e-12LF,variance))return false;
            continue;
        }
        double radius=1.LF+.5LF*dot(d,d);
        double weight=1.LF/(radius*radius*max(1e-12LF,variance));
        double normalized=weight/total,dx=d.x-mx,dy=d.y-my;
        double cx=normalized*(ixx*dx+ixy*dy),cy=normalized*(ixy*dx+iyy*dy);
        double coefficient=valueComplete?kernel(d.x)*kernel(d.y):normalized-mx*cx-my*cy;
        sum+=coefficient;sumDx+=cx;sumDy+=cy;coefficientMagnitude+=abs(coefficient);
        result.value+=coefficient*v;result.variance+=coefficient*coefficient*variance;
        result.gx+=cx*v;result.gy+=cy*v;
        result.dark+=coefficient*coefficient*gain*gain*params[4*site+1];
        double step=gain/params[20+site];result.dn+=coefficient*coefficient*step*step;
        double residual=v-prediction,standardized=residual*residual/max(1e-12LF,variance);
        if(!finiteValue(standardized)||standardized>16.LF)return false;
        normalizedResidual+=standardized;
        weightedResidual+=weight*residual*residual;weightedNoise+=weight*variance;
        double slopeRisk=risk(.5LF*dvec2(cx,cy),mapping);
        if(slopeRisk<0.LF)return false;
        gradientNoiseRisk+=variance*slopeRisk;
    }
    if(normalizedResidual>4.LF*double(count-3)||coefficientMagnitude>2.LF||
        result.variance>4.LF*max(1e-12LF,centralVariance)||abs(sum-1.LF)>1e-12LF||
        abs(sumDx)>1e-12LF||abs(sumDy)>1e-12LF)return false;
    result.model=max(0.LF,(weightedResidual-weightedNoise)/total)+gradientNoiseRisk;
    result.headroom=safety/magnitude;
    return finiteValue(result.value)&&finiteValue(result.variance)&&result.variance>=0.LF&&
        finiteValue(result.gx)&&finiteValue(result.gy)&&finiteValue(result.dark)&&
        finiteValue(result.dn)&&result.dn>0.LF&&finiteValue(result.model)&&result.model>=0.LF&&
        finiteValue(result.headroom)&&result.headroom>=0.LF&&result.headroom<=1.LF;
}
)GLSL";

} // namespace Raw::Bracketing::TemporalShaders
