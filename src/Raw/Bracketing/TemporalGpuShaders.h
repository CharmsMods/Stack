#pragma once
#include <string>

namespace Raw::Bracketing::TemporalShaders {
// Double-precision storage/calculation follows Merge.cpp, DetailFusion.h and
// SameCfaSampler. No half precision, denoising approximation or atomic sums.
inline const char* Common=R"GLSL(
#version 430 core
layout(local_size_x=64) in;
struct Observation {
    double value,variance,headroom,support,fallback,exposure,measurement,uncertainty;
    uint flags; uint padding;
};
struct Accumulator {
    Observation o;
    double weight,squared;
    double bw,dw,bs,ds,base,detail,bv,dv,cv,bu,du;
};
struct Mapping {double x,y,xx,xy,yy,confidence;};
layout(std430,binding=0) readonly buffer Source {vec4 taps[];};
layout(std430,binding=1) readonly buffer Motion {Mapping maps[];};
layout(std430,binding=2) buffer Guides {dvec4 guides[];};
layout(std430,binding=3) buffer Samples {Observation samples[];};
layout(std430,binding=4) buffer Accumulation {Accumulator sums[];};
layout(std430,binding=5) buffer Parameters {double params[24]; uint failed;};
uniform ivec4 region; // source left, top, width, height
uniform ivec4 target; // output left, top, width, height
uniform ivec2 sensor;
uniform ivec4 sites;
uniform int aligned,reference,groupIndex,useBands,localGuide,groupCount,initializeGuide;
const uint FINITE=1u,CLIPPED=2u,REJECTED=4u,REFERENCE=8u,FALLBACK_CLIP=16u;
// Transient accumulator provenance; not exported as a measurement flag.
const uint FALLBACK_REJECTED=32u;
bool finiteValue(double x){return !isnan(x)&&!isinf(x);}
Observation emptyObservation(){return Observation(0.LF,0.LF,0.LF,0.LF,0.LF,0.LF,-1.LF,0.LF,0u,0u);}
int siteAt(ivec2 p){return sites[(p.y&1)*2+(p.x&1)];}
bool inside(ivec2 p){return all(greaterThanEqual(p,ivec2(0)))&&all(lessThan(p,sensor));}
vec4 tapAt(ivec2 p){
    ivec2 q=p-region.xy;
    if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,region.zw))){atomicOr(failed,1u);return vec4(0,1,2,0);}
    return taps[q.y*region.z+q.x];
}
double noiseVariance(int site,double gain,double pilot){
    double shot=gain*params[4*site],offset=gain*gain*params[4*site+1];
    return shot*max(0.LF,pilot)+offset;
}
double residualVariance(int site,double pilot){
    double a=params[4*site+2],b=params[4*site+3];
    return a*a+b*b*max(0.LF,pilot)*max(0.LF,pilot);
}
bool badTap(vec4 t){return !finiteValue(double(t.x))||!finiteValue(double(t.y))||t.y<=0||((uint(t.z)&6u)!=0u);}
double headroom(double v){double h=clamp((.995LF-v)/.045LF,0.LF,1.LF);return h*h*(3.LF-2.LF*h);}
double risk(dvec2 gradient,Mapping m){
    if(!finiteValue(m.xx)||!finiteValue(m.xy)||!finiteValue(m.yy))return -1.LF;
    double xx=max(0.LF,m.xx),yy=max(0.LF,m.yy),xy=m.xy;
    if(m.xx< -1e-12LF||m.yy< -1e-12LF||xx*yy-xy*xy< -1e-12LF*max(1.LF,xx*yy))return -1.LF;
    if(xx*yy-xy*xy<0.LF)xy=sign(xy)*sqrt(xx*yy);
    double v=gradient.x*gradient.x*xx+2.LF*gradient.x*gradient.y*xy+gradient.y*gradient.y*yy;
    return finiteValue(v)&&v>=-1e-12LF?max(0.LF,v):-1.LF;
}
)GLSL";

inline const char* Prepare=R"GLSL(
void main(){
    uint i=gl_GlobalInvocationID.x;if(i>=uint(target.z*target.w))return;
    if(initializeGuide!=0){
    ivec2 q=ivec2(int(i)%target.z,int(i)/target.z),p=target.xy+q;
    double pilot=0.LF;int used=0;
    for(int dy=0;dy<4;dy+=2)for(int dx=0;dx<4;dx+=2){
        ivec2 r=(p/4)*4+ivec2(q.x%2+dx,q.y%2+dy);
        if(any(lessThan(r,target.xy))||any(greaterThanEqual(r,target.xy+target.zw)))continue;
        vec4 t=tapAt(r);if((uint(t.z)&15u)!=0u)continue;
        precise float product=t.x*t.y;double v=double(product);
        if(finiteValue(v)){pilot+=v;++used;}
    }
    vec4 center=tapAt(p);precise float product=center.x*center.y;
    double point=(uint(center.z)&4u)!=0u?0.LF:double(product);
    pilot=max(0.LF,used>0?pilot/double(used):(finiteValue(point)?point:0.LF));
    double gx=0.LF,gy=0.LF,mean=0.LF;int count=0;
    bool border=any(lessThan(p,ivec2(4)))||any(greaterThanEqual(p+ivec2(4),sensor));
    double sx=0.LF,sy=0.LF,sxx=0.LF,sxy=0.LF,syy=0.LF;int available=0;
    if(localGuide!=0)for(int dy=-4;dy<=4;dy+=2)for(int dx=-4;dx<=4;dx+=2){
        ivec2 r=p+ivec2(dx,dy);if(!inside(r))continue;
        if(border)++available;
        vec4 t=tapAt(r);double v=double(t.x)*double(t.y);
        if(!finiteValue(v)||uint(t.z)!=0u)continue;
        gx+=double(dx)*v;gy+=double(dy)*v;mean+=v;++count;
        if(border){sx+=double(dx);sy+=double(dy);sxx+=double(dx*dx);sxy+=double(dx*dy);syy+=double(dy*dy);}
    }
    double gradientNoiseFactor=0.LF;bool gradientValid=false;
    if(count==25){
        gx/=200.LF;gy/=200.LF;mean/=25.LF;
        gradientValid=true;
    }else if(border&&count==available&&count>=6){
        mean/=double(count);sxx-=sx*sx/double(count);sxy-=sx*sy/double(count);syy-=sy*sy/double(count);
        double determinant=sxx*syy-sxy*sxy;
        if(determinant>1e-12LF){
            double bx=gx-sx*mean,by=gy-sy*mean;
            gx=(syy*bx-sxy*by)/determinant;gy=(sxx*by-sxy*bx)/determinant;
            gradientNoiseFactor=(sxx+syy)/determinant;gradientValid=true;
        }
    }
    if(gradientValid){
        double variance=noiseVariance(siteAt(p),double(center.y),max(0.LF,mean));
        double energy=gx*gx+gy*gy;
        double noiseEnergy=count==25?2.LF*variance/200.LF:gradientNoiseFactor*variance;
        double retained=energy>0.LF?sqrt(max(0.LF,energy-noiseEnergy)/energy):0.LF;
        guides[i]=dvec4(pilot,gx*retained,gy*retained,1.LF);
    }else guides[i]=dvec4(pilot,0.LF,0.LF,0.LF);
    }
    for(int g=0;g<groupCount;++g){
        Accumulator a;a.o=emptyObservation();a.weight=0.LF;a.squared=0.LF;
        a.bw=0.LF;a.dw=0.LF;a.bs=0.LF;a.ds=0.LF;a.base=0.LF;a.detail=0.LF;
        a.bv=0.LF;a.dv=0.LF;a.cv=0.LF;a.bu=0.LF;a.du=0.LF;
        sums[g*target.z*target.w+int(i)]=a;
    }
}
)GLSL";

inline const char* Sample=R"GLSL(
double kernel(double d){double t=abs(d);if(t<=1.LF)return 1.5LF*t*t*t-2.5LF*t*t+1.LF;if(t<2.LF)return -.5LF*t*t*t+2.5LF*t*t-4.LF*t+2.LF;return 0.LF;}
double derivative(double d){double t=abs(d);if(t==0.LF||t>=2.LF)return 0.LF;return sign(d)*(t<=1.LF?4.5LF*t*t-5.LF*t:-1.5LF*t*t+5.LF*t-4.LF);}
Observation sampleAt(int i,ivec2 p){
    Observation o=emptyObservation();double scale=params[16],scaleVariance=params[17],pilot=guides[i].x;
    int site=siteAt(p);
    if(aligned==0){
        vec4 t=tapAt(p);double gain=double(t.y)*scale;
        o.value=double(t.x)*gain;o.fallback=o.value;o.exposure=1.LF/scale;
        if(!badTap(t)&&finiteValue(o.value)&&finiteValue(gain)&&gain>0.LF)o.flags|=FINITE;
        if((uint(t.z)&9u)!=0u)o.flags|=CLIPPED|FALLBACK_CLIP;
        if((o.flags&(FINITE|CLIPPED))==FINITE){
            o.measurement=max(1e-12LF,noiseVariance(site,gain,pilot));
            o.uncertainty=residualVariance(site,pilot)+pilot*pilot*scaleVariance/(scale*scale);
            o.variance=o.measurement+o.uncertainty;o.headroom=headroom(double(t.x));o.support=1.LF;
            if(reference!=0)o.flags|=REFERENCE;
        }
        return o;
    }
    Mapping m=maps[i];dvec2 plane=(dvec2(m.x,m.y)-dvec2(p&1))*.5LF;
    if(!finiteValue(plane.x)||!finiteValue(plane.y))return o;
    ivec2 extent=(sensor-(p&1)+1)/2;
    if(m.confidence<=0.LF)o.flags|=REJECTED;
    // Entirely outside the sensor: avoid converting an unbounded warp to int.
    if(any(lessThan(plane,dvec2(-1)))||any(greaterThan(plane,dvec2(extent))))return o;
    // llround rounds halfway away from zero, including the outside border.
    ivec2 nearest=ivec2(sign(plane)*floor(abs(plane)+.5LF));
    bool nearestInside=all(greaterThanEqual(nearest,ivec2(0)))&&all(lessThan(nearest,extent));
    double nearestValue=0.LF;
    if(nearestInside){vec4 t=tapAt(nearest*2+(p&1));nearestValue=double(t.x);
        if(!badTap(t)){o.flags|=FINITE;o.fallback=double(t.x)*double(t.y)*scale;o.exposure=1.LF/scale;
            if((uint(t.z)&9u)!=0u)o.flags|=CLIPPED|FALLBACK_CLIP;}}
    ivec2 base=ivec2(floor(plane));bool interior=all(greaterThanEqual(base,ivec2(1)))&&all(lessThan(base+2,extent));
    if(!interior&&(any(lessThan(plane,dvec2(0)))||any(greaterThan(plane,dvec2(extent-1)))||any(lessThan(extent,ivec2(2)))))return o;
    if(!interior)base=min(base,extent-2);
    double value=0.LF,variance=0.LF,gx=0.LF,gy=0.LF,fallback=0.LF,dark=0.LF,dn=0.LF;bool valid=true,validFallback=true,clippedFallback=false;
    double sum=0.LF,sumDx=0.LF,sumDy=0.LF;
    for(int k=0;k<16;++k){
        ivec2 txy;double w,dx,dy;
        if(interior){txy=base+ivec2(k%4-1,k/4-1);dvec2 d=plane-dvec2(txy);
            w=kernel(d.x)*kernel(d.y);dx=derivative(d.x)*kernel(d.y);dy=kernel(d.x)*derivative(d.y);
        }else{
            int j=k<4?k:0;txy=base+ivec2(j%2,j/2);dvec2 f=plane-dvec2(base);
            w=k<4?(j%2==0?1.LF-f.x:f.x)*(j/2==0?1.LF-f.y:f.y):0.LF;
            dx=k<4?(j%2==0?-1.LF:1.LF)*(j/2==0?1.LF-f.y:f.y):0.LF;
            dy=k<4?(j/2==0?-1.LF:1.LF)*(j%2==0?1.LF-f.x:f.x):0.LF;
        }
        sum+=w;sumDx+=dx;sumDy+=dy;
        vec4 t=tapAt(txy*2+(p&1));double gain=scale*double(t.y),v=gain*double(t.x);
        bool bad=badTap(t);if(bad||(uint(t.z)&9u)!=0u)valid=false;
        if(abs(w)>1e-12LF){if(bad)validFallback=false;
            fallback+=w*double(t.x)*double(t.y)*scale;clippedFallback=clippedFallback||((uint(t.z)&9u)!=0u);}
        value+=w*v;gx+=dx*v;gy+=dy*v;
        double noise=noiseVariance(site,gain,pilot);variance+=w*w*noise;
        dark+=w*w*(gain*gain*params[4*site+1]);double step=gain/params[20+site];dn+=w*w*step*step;
        if(!finiteValue(gain)||gain<=0.LF||!finiteValue(noise)||noise<0.LF)valid=false;
    }
    if(abs(sum-1.LF)>1e-12LF||abs(sumDx)>1e-12LF||abs(sumDy)>1e-12LF)valid=false;
    double reg=risk(dvec2(gx,gy)*.5LF,m),scaleRisk=scaleVariance/(scale*scale)*value*value,residual=residualVariance(site,pilot);
    if(reg<0.LF||!finiteValue(value)||!finiteValue(variance)||variance<0.LF||!finiteValue(dn)||dn<=0.LF||
        !finiteValue(scaleRisk)||!finiteValue(residual)||!finiteValue(dark))valid=false;
    if(!valid){if(validFallback&&finiteValue(fallback)){o.fallback=fallback;o.exposure=1.LF/scale;o.flags|=FINITE;o.flags&=~(CLIPPED|FALLBACK_CLIP);
            if(clippedFallback)o.flags|=CLIPPED|FALLBACK_CLIP;}return o;}
    o.fallback=value;o.flags|=FINITE;o.flags&=~(CLIPPED|FALLBACK_CLIP);
    if(m.confidence<=0.LF)return o;
    if(guides[i].w>0.LF){double sceneRisk=risk(guides[i].yz,m);if(sceneRisk>=0.LF)reg=sceneRisk;}
    o.value=value;o.measurement=max(1e-12LF,variance);o.uncertainty=residual+scaleRisk+reg/max(.20LF,m.confidence);
    o.variance=o.measurement+o.uncertainty;o.headroom=nearestInside?headroom(nearestValue):1.LF;o.support=1.LF;
    return o;
}
void main(){int i=int(gl_GlobalInvocationID.x);if(i>=target.z*target.w)return;
    samples[i]=sampleAt(i,target.xy+ivec2(i%target.z,i/target.z));}
)GLSL";

inline const char* Accumulate=R"GLSL(
void main(){
    int i=int(gl_GlobalInvocationID.x);if(i>=target.z*target.w)return;
    int index=groupIndex*target.z*target.w+i;Accumulator a=sums[index];Observation s=samples[i];
    a.o.flags|=s.flags&REJECTED;double contribution=0.LF;
    if((s.flags&FINITE)!=0u){
        bool oldRejected=(a.o.flags&FALLBACK_REJECTED)!=0u,newRejected=(s.flags&REJECTED)!=0u;
        if((a.o.flags&FINITE)==0u||(oldRejected&&!newRejected)||
           (oldRejected==newRejected&&s.exposure<a.o.exposure)){
            a.o.fallback=s.fallback;a.o.exposure=s.exposure;
            a.o.flags=(a.o.flags&~(CLIPPED|FALLBACK_CLIP|FALLBACK_REJECTED))|
                (s.flags&(CLIPPED|FALLBACK_CLIP))|(newRejected?FALLBACK_REJECTED:0u);}
        a.o.flags|=FINITE;
        if(s.support>0.LF){
            double h=finiteValue(s.headroom)?clamp(s.headroom,0.LF,1.LF):0.LF;
            if(h>0.LF&&a.o.headroom<=0.LF&&a.weight>0.LF){
                a.o.value=0.LF;a.o.variance=0.LF;a.o.measurement=0.LF;a.o.uncertainty=0.LF;
                a.o.flags&=~REFERENCE;a.weight=0.LF;a.squared=0.LF;
                a.bw=0.LF;a.dw=0.LF;a.bs=0.LF;a.ds=0.LF;a.base=0.LF;a.detail=0.LF;
                a.bv=0.LF;a.dv=0.LF;a.cv=0.LF;a.bu=0.LF;a.du=0.LF;
            }
            contribution=h>0.LF?h:(a.o.headroom>0.LF?0.LF:1.LF);
            if(contribution>0.LF){
                a.o.flags|=s.flags&REFERENCE;double w=contribution/s.variance;
                if(a.weight==0.LF)a.o.measurement=0.LF;
                a.o.value+=w*s.value;a.o.variance+=w*w*s.variance;a.o.headroom+=w*h;
                a.o.measurement+=w*w*s.measurement;a.o.uncertainty+=w*w*s.uncertainty;
                a.weight+=w;a.squared+=w*w;
            }
        }
    }
    if(useBands!=0&&contribution>0.LF&&s.support>0.LF&&(s.flags&(FINITE|CLIPPED))==FINITE){
        ivec2 p=ivec2(i%target.z,i/target.z);double coarse=0.LF,cv=0.LF,total=0.LF,cw=0.LF;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            ivec2 q=p+2*ivec2(dx,dy);if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,target.zw)))continue;
            Observation n=samples[q.y*target.z+q.x];if((n.flags&(FINITE|CLIPPED))!=FINITE||n.support<=0.LF)continue;
            double w=double((dx==0?2:1)*(dy==0?2:1));coarse+=w*n.value;cv+=w*w*n.measurement;total+=w;
            if(dx==0&&dy==0)cw=w;
        }
        double covariance=s.measurement;
        if(total>0.LF){coarse/=total;double area=aligned!=0?2.5LF:1.LF;cv*=area/(total*total);covariance=min(s.measurement,area*cw/total*s.measurement);}
        else{coarse=s.value;cv=s.measurement;}
        double w=contribution/max(1e-12LF,s.variance),d=w*clamp(params[18],.05LF,1.LF);
        covariance=clamp(covariance,0.LF,sqrt(max(0.LF,cv*s.measurement)));
        a.bw+=w;a.dw+=d;a.bs+=w*w;a.ds+=d*d;a.base+=w*coarse;a.detail+=d*(s.value-coarse);
        a.bv+=w*w*cv;a.dv+=d*d*max(0.LF,s.measurement+cv-2.LF*covariance);a.cv+=w*d*(covariance-cv);
        a.bu+=w*w*s.uncertainty;a.du+=d*d*s.uncertainty;
    }
    sums[index]=a;
}
)GLSL";

inline const char* Finish=R"GLSL(
void main(){
    int i=int(gl_GlobalInvocationID.x);if(i>=target.z*target.w)return;
    int index=groupIndex*target.z*target.w+i;Accumulator a=sums[index];
    if(a.weight>0.LF){
        a.o.value/=a.weight;a.o.variance/=a.weight*a.weight;a.o.measurement/=a.weight*a.weight;
        a.o.uncertainty/=a.weight*a.weight;a.o.headroom/=a.weight;a.o.support=a.weight*a.weight/a.squared;a.o.flags&=~CLIPPED;
        if(a.bw>0.LF&&a.dw>0.LF){
            a.o.value=a.base/a.bw+a.detail/a.dw;
            a.o.measurement=max(1e-12LF,a.bv/(a.bw*a.bw)+a.dv/(a.dw*a.dw)+2.LF*a.cv/(a.bw*a.dw));
            a.o.uncertainty=a.bu/(a.bw*a.bw)+a.du/(a.dw*a.dw);a.o.variance=a.o.measurement+a.o.uncertainty;
            a.o.support=min(a.bw*a.bw/a.bs,a.dw*a.dw/a.ds);
        }
    }
    samples[i]=a.o;
}
)GLSL";
}
