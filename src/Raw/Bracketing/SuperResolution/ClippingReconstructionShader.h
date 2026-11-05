#pragma once

namespace Raw::Bracketing::Sr {

// Scalar counterpart: ClippingReconstruction.h. This fragment follows the
// input buffer/uniform declarations and uses the existing bounded RAW region.
inline const char* clippingReconstructionShader=R"GLSL(
struct BroadCorrection {
    vec3 coefficients;
    float mass,modelVariance;
};
float clippingSmooth(float low,float high,float value) {
    float t=clamp((value-low)/(high-low),0.,1.);
    return t*t*(3.-2.*t);
}
float clippingCoreInfluence(vec2 d) {
    return 1.-clippingSmooth(2.25,6.25,dot(d,d));
}
float clippingSpatialWeight(vec4 kernel,vec2 d) {
    return exp(-.5*(kernel.x*d.x*d.x+2.*kernel.y*d.x*d.y+kernel.z*d.y*d.y));
}
float clippingSensorVariance(vec4 tap,float pilot) {
    return max(1e-10,tap.y*max(0.,pilot)+tap.z);
}
float clippingFitWeight(vec4 tap,float spatial,float variance,float risk) {
    return 1e-5*spatial*clamp(tap.w,0.,1.)/(variance+risk);
}
bool clippingFinite(vec4 tap) {
    return !any(isnan(tap))&&!any(isinf(tap));
}
float clippingCoreHeadroom(ivec4 region,vec4 mapping) {
    ivec2 center=ivec2(floor(mapping.xy+.5));
    float headroom=1.;
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
        ivec2 q=center+ivec2(x,y),loc=q-region.xy;
        float influence=clippingCoreInfluence(vec2(q)-mapping.xy);
        if(influence<=0.)continue;
        if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw))) {
            headroom=min(headroom,1.-influence);continue;
        }
        vec4 tap=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x];
        float safety=clippingFinite(tap)?clamp(tap.w,0.,1.):0.;
        headroom=min(headroom,1.-influence*(1.-safety));
    }
    return headroom;
}
bool reconstructClipped(ivec4 region,ivec4 colors,vec4 mapping,vec4 kernel,
    float pilot,float recoveredHeadroom,out BroadCorrection correction[3]) {
    for(int c=0;c<3;++c)correction[c]=BroadCorrection(vec3(0),0.,0.);
    if(!(recoveredHeadroom>0.))return false;
    ivec2 center=ivec2(floor(mapping.xy+.5));
    float risk=max(0.,mapping.w),confidence=1.;
    // Fit one color at a time to keep per-invocation storage bounded. Admission
    // is shared across RGB, including the least certain color's fit quality.
    for(int color=0;color<3;++color) {
        float total=0.,sx=0.,sy=0.,sxx=0.,sxy=0.,syy=0.,sv=0.,sxv=0.,syv=0.;
        float fullSpatial=0.,validSpatial=0.,softCount=0.;
        for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
            ivec2 q=center+ivec2(x,y),loc=q-region.xy;
            if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw))||
                colors[((q.y&1)<<1)|(q.x&1)]!=color)continue;
            vec4 tap=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x];
            if(tap.w==-2.)return false;
            vec2 d=vec2(q)-mapping.xy;
            float spatial=clippingSpatialWeight(kernel,d);
            fullSpatial+=spatial;
            if(tap.w<=0.)continue;
            if(!clippingFinite(tap))return false;
            float variance=clippingSensorVariance(tap,pilot);
            float weight=clippingFitWeight(tap,spatial,variance,risk);
            validSpatial+=spatial*clamp(tap.w,0.,1.);
            softCount+=clippingSmooth(0.,.05,tap.w);
            total+=weight;sx+=weight*d.x;sy+=weight*d.y;
            sxx+=weight*d.x*d.x;sxy+=weight*d.x*d.y;syy+=weight*d.y*d.y;
            sv+=weight*tap.x;sxv+=weight*d.x*tap.x;syv+=weight*d.y*tap.x;
        }
        if(!(fullSpatial>0.)||isnan(total)||isinf(total)||total<=0.)return false;
        float mx=sx/total,my=sy/total,mv=sv/total;
        float xx=sxx/total-mx*mx,xy=sxy/total-mx*my,yy=syy/total-my*my;
        float trace=xx+yy,determinant=xx*yy-xy*xy;
        if(isnan(trace)||isinf(trace)||isnan(determinant)||isinf(determinant)||
            xx<=0.||yy<=0.||determinant<=0.||determinant<1e-4*trace*trace)return false;
        float ixx=yy/determinant,ixy=-xy/determinant,iyy=xx/determinant;
        float vx=sxv/total-mx*mv,vy=syv/total-my*mv;
        float slopeX=ixx*vx+ixy*vy,slopeY=ixy*vx+iyy*vy;
        float value=mv-slopeX*mx-slopeY*my;
        if(any(isnan(vec3(value,slopeX,slopeY)))||any(isinf(vec3(value,slopeX,slopeY))))return false;
        float condition=determinant/(trace*trace);
        float leverage=mx*(ixx*mx+ixy*my)+my*(ixy*mx+iyy*my);
        if(isnan(condition)||isinf(condition)||isnan(leverage)||isinf(leverage)||leverage<0.)return false;
        confidence=min(confidence,min(clippingSmooth(.5,.85,validSpatial/fullSpatial),
            min(clippingSmooth(4.,6.,softCount),min(clippingSmooth(.001,.01,condition),
            1.-clippingSmooth(.25,1.,leverage)))));
        if(!(confidence>0.))return false;

        float variance=0.,gradientXVariance=0.,gradientYVariance=0.,meanVariance=0.;
        float residual=0.,expectedNoise=0.,standardizedResidual=0.,coefficientNorm=0.;
        for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
            ivec2 q=center+ivec2(x,y),loc=q-region.xy;
            if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw))||
                colors[((q.y&1)<<1)|(q.x&1)]!=color)continue;
            vec4 tap=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x];
            if(tap.w<=0.)continue;
            vec2 d=vec2(q)-mapping.xy;
            float v=clippingSensorVariance(tap,pilot);
            float weight=clippingFitWeight(tap,clippingSpatialWeight(kernel,d),v,risk);
            float normalized=weight/total,dx=d.x-mx,dy=d.y-my;
            float gx=normalized*(ixx*dx+ixy*dy),gy=normalized*(ixy*dx+iyy*dy);
            float coefficient=normalized-mx*gx-my*gy;
            float difference=tap.x-(value+slopeX*d.x+slopeY*d.y);
            variance+=coefficient*coefficient*v;
            gradientXVariance+=gx*gx*v;gradientYVariance+=gy*gy*v;
            meanVariance+=normalized*normalized*v;
            residual+=normalized*difference*difference;expectedNoise+=normalized*v;
            standardizedResidual+=normalized*difference*difference/v;
            coefficientNorm+=abs(coefficient);
        }
        if(!clippingFinite(vec4(variance,gradientXVariance,gradientYVariance,meanVariance))||
            !clippingFinite(vec4(residual,expectedNoise,standardizedResidual,coefficientNorm)))return false;
        float modelVariance=max(0.,residual-expectedNoise);
        float noiseAmplification=(variance+modelVariance)/max(1e-20,meanVariance);
        confidence=min(confidence,min(1.-clippingSmooth(1.25,2.,coefficientNorm),
            min(1.-clippingSmooth(3.,9.,standardizedResidual),
            min(1.-clippingSmooth(4.,8.,noiseAmplification),
            min(1.,(meanVariance+risk)/(variance+modelVariance+risk))))));
        if(isnan(variance)||isinf(variance)||isnan(modelVariance)||isinf(modelVariance)||
            !(confidence>0.))return false;

        for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
            ivec2 q=center+ivec2(x,y),loc=q-region.xy;
            if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw))||
                colors[((q.y&1)<<1)|(q.x&1)]!=color)continue;
            vec4 tap=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x];
            if(tap.w<0.||tap.w>=1.)continue;
            if(!clippingFinite(tap))return false;
            vec2 d=vec2(q)-mapping.xy;
            float prediction=value+slopeX*d.x+slopeY*d.y;
            if(isnan(prediction)||isinf(prediction))return false;
            float deficit=max(0.,tap.x-prediction);
            float fitSigma=sqrt(variance)+abs(d.x)*sqrt(gradientXVariance)+
                abs(d.y)*sqrt(gradientYVariance)+sqrt(modelVariance);
            float sigma=fitSigma+sqrt(clippingSensorVariance(tap,max(pilot,tap.x))+risk);
            if(isnan(sigma)||isinf(sigma))return false;
            confidence=min(confidence,
                1.-(1.-tap.w)*clippingSmooth(2.,4.,deficit/max(1e-10,sigma)));
        }
        if(!(confidence>0.))return false;
        correction[color].coefficients=vec3(1.+leverage,-(mx*ixx+my*ixy),-(mx*ixy+my*iyy));
        correction[color].mass=total;correction[color].modelVariance=modelVariance;
    }
    float scale=mapping.z*recoveredHeadroom*confidence;
    for(int c=0;c<3;++c) {
        correction[c].coefficients*=scale;correction[c].mass*=scale;
    }
    return true;
}
)GLSL";

} // namespace Raw::Bracketing::Sr
